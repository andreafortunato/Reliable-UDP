#ifndef SERVER_H
#define SERVER_H

#include "Common.h"

#define NOFILE "File Not Found!"

/* Struttura associata al singolo client */
typedef struct _ClientNode {
	unsigned int sockfd;					/* Descrittore del socket */
	char ip[16];							/* IP Address */
	unsigned int clientPort;				/* Porta sorgente del Client */
	struct sockaddr_in connection;			/* Struttura associata ai dati del Client */
	int addrlenSocket;
	unsigned int serverPort;				/* Porta del server riservata al Client */

	// unsigned int lastSeqClient;				/* Ultimo numero di sequenza ricevuto dal Client */
	// unsigned int lastSeqServer;				/* Ultimo numero di sequenza inviato al Client */

	pthread_mutex_t lockTid;
	pthread_t handTid;						/* ID del Thread Handshake */
	pthread_t sendTid;						/* ID del Thread Send */
	pthread_t recvTid;						/* ID del Thread Recv */
	pthread_t timeTid;						/* ID del Thread Timeout */
	int recvDead;
	int mustDie[4];

	SegQueue *queueHead;
	double RTO;
	struct timeval segTimeout[WIN_SIZE];
	struct timeval segRtt[WIN_SIZE];
	int sendQueue;
	Segment sndWindow[WIN_SIZE];
	Segment rcvWindow[WIN_SIZE];
	int segToSend[WIN_SIZE];
	int segSent[WIN_SIZE];
	int rcvWinFlag[WIN_SIZE];
	int rttFlag[WIN_SIZE];
	int sndWinPos;
	int sndPos;	
	
	FILE *fileDescriptor;

	pthread_mutex_t queueLock;
	pthread_rwlock_t slideLock;	

	struct _ClientNode *next;				/* Puntatore a ClientNode successivo */
} ClientNode;


/* Struttura per il passaggio di dati alla creazione di un nuovo thread */
typedef struct _ThreadArgs {
	Sockaddr_in clientSocket;
	Segment segment;
} ThreadArgs;

typedef struct _FileNode {
	char *fileName;
	int available;

	struct _FileNode *next;
} FileNode;


/* ************************************************************************************************************************************* */

FileNode* newFileNode(char *fileName) {
	FileNode *file = (FileNode *) malloc(sizeof(FileNode));
	if(file != NULL)
	{	
		bzero(file, sizeof(FileNode));
		file -> fileName = (char *) malloc(sizeof(char)*strlen(fileName) + 1);
		if(file -> fileName != NULL) {
			strcpy(file -> fileName, fileName);
		} else {
			printf("Error while trying to \"malloc\" a new fileName!\nClosing...\n");
			exit(-1);
		}
		file -> available = 0;
	} else {
		printf("Error while trying to \"malloc\" a new FileNode!\nClosing...\n");
		exit(-1);
	}

	return file;
}


ThreadArgs* newThreadArgs(Sockaddr_in clientSocket, Segment segment) {
	ThreadArgs *threadArgs = (ThreadArgs *) malloc(sizeof(ThreadArgs));
	if(threadArgs != NULL)
	{	
		bzero(threadArgs, sizeof(ThreadArgs));
		threadArgs -> clientSocket = clientSocket;
		threadArgs -> segment = segment;
	} else {
		printf("Error while trying to \"malloc\" a new ThreadArgs!\nClosing...\n");
		exit(-1);
	}

	return threadArgs;
}

/* Inizializzazione di un nuovo client */
ClientNode* newNode(unsigned int sockfd, struct sockaddr_in clientSocket, pthread_t handTid, unsigned int serverPort) {
	ClientNode *node = (ClientNode *) malloc(sizeof(ClientNode));
	if(node != NULL)
	{	
		bzero(node, sizeof(ClientNode));
		node -> sockfd = sockfd;
		strcpy(node -> ip, inet_ntoa(clientSocket.sin_addr));
		node -> clientPort = ntohs(clientSocket.sin_port);
		node -> connection = clientSocket;
		node -> addrlenSocket = sizeof(clientSocket);
		node -> serverPort = serverPort;

		node -> handTid = handTid;
		node -> sendTid = -1;
		node -> recvTid = -1;
		node -> timeTid = -1;
		node -> recvDead = 0;

		for (int i=0; i<4; i++)
			node -> mustDie[i] = 0;

		node -> queueHead = NULL;
		node -> RTO = 5000;
		node -> sendQueue = 1; /* Abilita lettura della coda per il thread send */
		for(int i=0; i<WIN_SIZE; i++) {
			(node -> segToSend)[i] = 0;
			(node -> segSent)[i] = 0;
			(node -> rttFlag)[i] = 0;
			(node -> rcvWinFlag)[i] = 0;
	    }
		node -> sndWinPos = 0;
		node -> sndPos = 0;
		
		node -> fileDescriptor = NULL;

		/* Inizializzazione semafori */
		if(pthread_mutex_init(&(node -> lockTid), NULL) != 0) {
        	printf("Failed queueLock semaphore initialization.\n");
            exit(-1);
        }
        if(pthread_mutex_init(&(node -> queueLock), NULL) != 0) {
        	printf("Failed queueLock semaphore initialization.\n");
            exit(-1);
        }
        if(pthread_rwlock_init(&(node -> slideLock), NULL) != 0) {
            printf("Failed slideLock semaphore initialization.\n");
            exit(-1);
        }        

		node -> next = NULL;
	} else {
		printf("Error while trying to \"malloc\" a new ClientNode!\nClosing...\n");
		exit(-1);
	}

	return node;
}

/* Informazioni sull'orario */
char *getTime() {
	time_t dateTime;
	struct tm* time_info;
	char *time_buff = malloc(32);


	time(&dateTime);
	time_info = localtime(&dateTime);
	strftime(time_buff, 32, "%H:%M:%S", time_info);

	return time_buff;
}

/* TEMP - Stampa lista */
void printList(ClientNode *clientList) {
	while(clientList != NULL)
	{
		printf("\n");
		printf("Sockfd: %d\nIP: %s\nPort: %d\n", clientList->sockfd, clientList->ip, clientList->clientPort);
		
		if(clientList->next)
			printf("Next: %d\n", clientList->next->clientPort);
		else
			printf("Next: NULL\n");

		clientList = clientList->next;
	}
	printf("|-----------------------------|\n");
}

/* Chiusura socket, flag 'free' pagina logica (memoria riutilizzabile) ed azzeramento del suo contenuto */
void empty(ClientNode *client) {
	// printf("Porta da eliminare: %d\n", client->clientPort);

	close(client -> sockfd);

	/* Distruzione semafori */
	if(pthread_mutex_destroy(&(client -> lockTid)) != 0) {
        printf("Failed (client -> slideLock) semaphore destruction.\n");
        exit(-1);
    }
    if(pthread_mutex_destroy(&(client -> queueLock)) != 0) {
        printf("Failed queueLock semaphore destruction.\n");
        exit(-1);
    }
    if(pthread_rwlock_destroy(&(client -> slideLock)) != 0) {
        printf("Failed (client -> slideLock) semaphore destruction.\n");
        exit(-1);
    }

    bzero(client, sizeof(ClientNode));
	free(client);
}

/* Aggiunge un nuovo client alla lista clientList */
void addClientNode(ClientNode **clientList, ClientNode *newClient) {
	if(*clientList == NULL) {
		*clientList = newClient;
	} else {
		ClientNode *head = *clientList;
		*clientList = newClient;
		newClient -> next = head;
	}
}

/* Elimina un client dalla lista clientList */
void deleteClientNode(ClientNode **clientList, ClientNode *client) {
	// printf("\n\n---> STAMPA LISTA PRE DELETE <---\n\n");
 //    printList(*clientList);

	ClientNode *current, *prev;

	current = *clientList;

	/* E' presente un solo nodo, ovvero client */
	if(current -> next == NULL) {
		*clientList = NULL;
	} else {
		prev = current;

		// while((current->sockfd) != (client->sockfd) || (current->clientPort) != (client->clientPort)){
		while(current != client){
			prev = current;
			current = current -> next;
		}

		if(current == *clientList)
			*clientList = current -> next;
		else
			prev -> next = current -> next;
	}

	empty(client);

	// printf("\n\n---> STAMPA LISTA POST DELETE <---\n\n");
 //    printList(*clientList);
}

/* Genera la lista dei file presenti della directory corrente del server, escluso
   il suo file eseguibile e tutte le eventuali sottocartelle. */
void getFileList(FileNode **fileListHead, char *serverFileName) {

	FileNode *fileNode = NULL;
	FileNode *tail = NULL;

    char listFileCmd[64];
    FILE *fp;
    char fileName[256];

    sprintf(listFileCmd, "ls -p | grep -v / | grep -v \"^%s$\"", serverFileName);

    fp = popen(listFileCmd, "r");
    if (fp == NULL){
        printf("Error while trying to run the 'List file' command!\nClosing...\n");
		exit(-1);
    }

    while (fgets(fileName, 256, fp) != NULL){
    	fileNode = newFileNode(fileName);
    	fileNode -> available = 1;

    	if(*fileListHead == NULL){
    		*fileListHead = fileNode;
    		tail = *fileListHead;
    	} else {
    		tail -> next = fileNode;
    		tail = tail -> next;
    	}
    }

    if (pclose(fp) == -1) {
        printf("Error while trying to close the process for 'List file' command!\nClosing...\n");
		exit(-1);
    }
}

/* Restituisce un lista dei file in formato stringa per il client */
char* fileNameListToString(FileNode *fileListHead) {

	char *fileList = malloc(1);
	if(fileList == NULL) {
		printf("Error while trying to \"malloc\" a new fileList!\nClosing...\n");
		exit(-1);
	}

	int size;

	while(fileListHead != NULL){
		size = strlen(fileList);
		fileList = realloc(fileList, size + strlen(fileListHead->fileName) + 1);
        if(fileList == NULL) {
        	printf("Error while trying to \"realloc\" the fileList string!\nClosing...\n");
			exit(-1);
        }
        strcat(fileList, fileListHead->fileName);

		fileListHead = fileListHead -> next;
	}
	
    return fileList;
}

/* Inserimento, ordinato per nome, di un nuovo file */
void addFile(FileNode** fileListHead, char* fileName) {
	FileNode *newFile = newFileNode(fileName);

	FileNode* current;
    /* Special case for the head end */
    if (*fileListHead == NULL || strcasecmp((*fileListHead)->fileName, fileName) > 0) {
    	newFile -> next = *fileListHead;
    	*fileListHead = newFile;
    } else {
        /* Locate the node before the point of insertion */
        current = *fileListHead;
        while (current->next != NULL && strcasecmp(current->next->fileName, fileName) < 0) {
            current = current->next;
        }
        newFile->next = current->next;
        current->next = newFile;
    }
}


/* Ritorna il nome originale del file 'clientFileName' se questo esiste, NULL altrimenti */
char *fileExists(FileNode *fileListHead, char *clientFileName/*, char* up_down*/) {
	char *originalFilename = NULL;
	char fileToSearch[strlen(clientFileName)+2];
	sprintf(fileToSearch, "%s\n", clientFileName);
	// strcpy(fileToSearch, clientFileName);
	// strcat(fileToSearch, "\n");


	while(fileListHead != NULL) {
		if(strcasecmp(fileToSearch, fileListHead -> fileName) == 0 && (fileListHead -> available == 1)) {
			originalFilename = malloc(strlen(fileListHead -> fileName));
			strcpy(originalFilename, fileListHead -> fileName);
			originalFilename[strlen(originalFilename)-1] = '\0';
			break;
		}
		fileListHead = fileListHead -> next;
	}

    return originalFilename;
}

void pthread_cancelAndWait(ClientNode *client, pthread_t tid) {
	// if(tid != -1) {
	// 	pthread_cancel(tid);
	// 	pthread_join(tid, NULL);

	// 	printf("Cancellato: %ld\n", tid);
	// }

	pthread_mutex_lock(&(client -> lockTid));
	if(tid != -1){
		if(tid == client->handTid) {
			client->handTid = -1;
		}
		else if(tid == client->timeTid) {
			client->timeTid = -1;
		}
		else if(tid == client->sendTid) {
			client->sendTid = -1;
		}
		else if(tid == client->recvTid) {
			client->recvTid = -1;
		}

		pthread_cancel(tid);
		pthread_join(tid, NULL);
		printf("Cancellato thread: %ld\n", tid);
	}
	pthread_mutex_unlock(&(client -> lockTid));
}

void checkIfMustDie(ClientNode *client, int threadNum) {
	if((client -> mustDie)[threadNum])
		pthread_exit(NULL);
}

#endif