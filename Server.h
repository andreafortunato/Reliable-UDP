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
 
	pthread_t tid;							/* Thread ID temporaneo associato al Client */
	unsigned int serverPort;				/* Porta del server riservata al Client*/

	unsigned int lastSeqClient;				/* Ultimo numero di sequenza ricevuto dal Client */
	unsigned int lastSeqServer;				/* Ultimo numero di sequenza inviato al Client */

	Segment *sendWindow;
	unsigned int winPos;

	int ack;

	struct _ClientNode *next;				/* Puntatore a ClientNode successivo */
	struct _ClientNode *prev;   			/* Puntatore a ClientNode precedente */
} ClientNode;


/* Struttura per il passaggio di dati alla creazione di un nuovo thread */
typedef struct _ThreadArgs {
	struct sockaddr_in clientSocket;
	Segment segment;
	ClientNode *client;
	/* var */
} ThreadArgs;


/* ************************************************************************************************************************************* */

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
ClientNode* newNode(unsigned int sockfd, char *ip, unsigned int clientPort, struct sockaddr_in clientSocket, pthread_t tid, unsigned int serverPort, char *lastSeqClient) {
	ClientNode *node = (ClientNode *) malloc(sizeof(ClientNode));
	if(node != NULL)
	{	
		bzero(node, sizeof(ClientNode));
		node -> sockfd = sockfd;
		strcpy(node -> ip, ip);
		node -> clientPort = clientPort;
		node -> connection = clientSocket;

		node -> tid = tid;
		node -> serverPort = serverPort;

		node -> lastSeqClient = atoi(lastSeqClient);
		node -> lastSeqServer = 0;

		// CAMBIARE '5' CON DIMENSIONE FINESTRA PASSATA COME PARAMETRO (?)
		node -> sendWindow = (Segment *) malloc(sizeof(Segment) * 5);
		if((node -> sendWindow) == NULL) {
			printf("Error while trying to \"malloc\" a new sendWindow of node (%s:%u)!\nClosing...\n", ip, clientPort);
			exit(-1);
		}
		node -> winPos = -1;

		node -> next = NULL;
		node -> prev = NULL;
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

/* Chiusura socket, flag 'free' pagina logica (memoria riutilizzabile) ed azzeramento del suo contenuto */
void empty(ClientNode *clientToBeDeleted) {
	close(clientToBeDeleted -> sockfd);
	free(clientToBeDeleted);
	bzero(clientToBeDeleted, sizeof(ClientNode));
}

/* Aggiunge un nuovo client alla lista clientList */
void addClientNode(ClientNode **clientList, ClientNode *newClient, int *clientListSize, int *maxSockFd) {
	ClientNode *tmp1, *tmp2;


	if((*clientListSize) == 0)
	{
		*clientList = newClient;
		*maxSockFd = newClient -> sockfd;
	}
	else
	{
		tmp1 = *clientList;

		while(tmp1 != NULL)
		{	
			tmp2 = tmp1;

			if((newClient -> sockfd) < (tmp1 -> sockfd)) {
				
				// Se tmp1 è la testa
				if(tmp1 == *clientList) {
					*clientList = newClient;
					newClient -> next = tmp1;
					tmp1 -> prev = newClient;
				}	
				// Se tmp1 non è la testa
				else {
					newClient -> next = tmp1;
					newClient -> prev = tmp1 -> prev;
					tmp1 -> prev -> next = newClient;
					tmp1 -> prev = newClient;
				}

				(*clientListSize)++;
				return;
			}
			
			tmp1 = tmp1 -> next;
		}

		// Se sono arrivato alla fine della lista
		tmp2 -> next = newClient;
		newClient -> prev = tmp2;
		*maxSockFd = newClient -> sockfd;
	}
	(*clientListSize)++;
}

/* Elimina un client dalla lista clientList */
void deleteClientNode(ClientNode **clientList, ClientNode *client, int *clientListSize, int *maxSockFd) {

	ClientNode *current;

	current = *clientList;

	// Se client è l'unico elemento presente o l'ultimo
	if(client -> next == NULL) {

		// Se è l'unico
		if(client -> prev == NULL) {
			printf("Eliminazione in testa, unico elemento\n");
			*maxSockFd = 0;
		}
		// Se è l'ultimo
		else {
			printf("Eliminazione in coda\n");
			client -> prev -> next = NULL;
			*maxSockFd = client -> prev -> sockfd;
		}

		empty(client);
		(*clientListSize)--;
		return;
	}

	/* Trovo la posizione di client, salvandola in 'current' */
	while(current != NULL)
	{
		if((current -> sockfd) == (client -> sockfd))
			break;

		current = current -> next;
	}
	printf("Porta da eliminare: %d\n", current->clientPort);
	/* Se l'elemento è in testa alla lista */
	if(current == *clientList)
	{
		printf("Eliminazione in testa\n");
		*clientList = current -> next;
		current -> next -> prev = NULL;
	}
	/* Se l'elemento non è in testa alla lista */
	else
	{
		printf("Eliminazione al centro\n");
		current -> prev -> next = current -> next;
		current -> next -> prev = current -> prev;
	}
	
	empty(client);
	(*clientListSize)--;

}

/* TEMP - Stampa lista */
void printList(ClientNode *clientList) {
	while(clientList != NULL)
	{
		printf("\n");
		if(clientList->prev)
			printf("Prev: %d\n", clientList->prev->clientPort);
		else
			printf("Prev: NULL\n");

		printf("Sockfd: %d\nIP: %s\nPort: %d\n", clientList->sockfd, clientList->ip, clientList->clientPort);
		
		if(clientList->next)
			printf("Next: %d\n", clientList->next->clientPort);
		else
			printf("Next: NULL\n");

		clientList = clientList->next;
	}
	printf("|-----------------------------|\n");
}

/* Ritorna in 'fileList' la lista dei file nella directory del server
   escluse le cartelle ed il file eseguibile del server */
char** getFileNameList(char *serverFileName, int *numFiles) {

    char listFileCmd[64];
    FILE *fp;
    char filename[256];

    char **fileList = NULL;

    sprintf(listFileCmd, "ls -p | grep -v / | grep -v \"%s\"", serverFileName);

    fp = popen(listFileCmd, "r");
    if (fp == NULL){
        printf("Error while trying to run the 'List file' command!\nClosing...\n");
		exit(-1);
    }

    while (fgets(filename, 256, fp) != NULL){
    	(*numFiles)++;
        fileList = realloc(fileList, sizeof(char*)*(*numFiles));
        if(fileList == NULL) {
        	printf("Error while trying to \"realloc\" the fileList string!\nClosing...\n");
			exit(-1);
		}
        bzero(fileList + ((*numFiles)-1), sizeof(char*));

        fileList[(*numFiles)-1] = malloc(strlen(filename));
        if(fileList[(*numFiles)-1] == NULL) {
        	printf("Error while trying to \"malloc\" fileList[%d]!\nClosing...\n", (*numFiles)-1);
			exit(-1);
        }
        bzero(fileList[(*numFiles)-1], strlen(filename));

        strcpy(fileList[(*numFiles)-1], filename);
    }

    if (pclose(fp) == -1) {
        printf("Error while trying to close the process for 'List file' command!\nClosing...\n");
		exit(-1);
    }

    return fileList;
}

/* Ritorna in 'fileList' la lista dei file nella directory del server
   escluse le cartelle ed il file eseguibile del server */
char *fileExist(char **fileNameList, char *clientFileName, int numFiles) {

	char *originalFilename = NULL;
	char fileToSearch[strlen(clientFileName)+1];
	strcpy(fileToSearch, clientFileName);
	char *str1;
	strcat(fileToSearch, "\n");
	char *str2 = tolowerString(fileToSearch);

	for(int i=0; i < numFiles; i++) {
		str1 = tolowerString(fileNameList[i]);
		if(strcmp(str1, str2) == 0) {
			originalFilename = malloc(strlen(fileNameList[i]));
			strcpy(originalFilename, fileNameList[i]);
			originalFilename[strlen(originalFilename)-1] = '\0';
			//printf("HO COPIATO IN ORIGINALFILENAME: %s, LEN: %ld\n\n", originalFilename, strlen(originalFilename));
			free(str1);
			break;
		}
		bzero(str1, strlen(str1));
		free(str1);
	}

	free(str2);
    return originalFilename;
}

/* Restituisce un lista dei file in formato stringa per il client */
char* fileNameListToString(char **fileNameList, int numFiles) {

	char *fileList = malloc(1);
	if(fileList == NULL) {
		printf("Error while trying to \"malloc\" a new fileList!\nClosing...\n");
		exit(-1);
	}
	bzero(fileList, 1);

	int size;
	for(int i=0; i < numFiles; i++) {
		size = strlen(fileList);
		fileList = realloc(fileList, size + strlen(fileNameList[i]) + 1);
        if(fileList == NULL) {
        	printf("Error while trying to \"realloc\" the fileList string!\nClosing...\n");
			exit(-1);
        }
        strcat(fileList, fileNameList[i]);
	}


    return fileList;
}

#endif