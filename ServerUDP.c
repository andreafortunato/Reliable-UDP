// https://packetlife.net/blog/2010/jun/7/understanding-tcp-sequence-acknowledgment-numbers/

// Server - FTP on UDP 
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include <ctype.h>
#include "Server.h"

#define IP_PROTOCOL 0

/* Prototipi */
void *client_thread_handshake(void *);      /* Thread associato al client */
void *client_thread_list(void *);
void *client_thread_download(void *);
void *client_thread_upload(void *);
void ctrl_c_handler();

/* Variabili globali */
pthread_rwlock_t lockList;                  /* Semaforo Read/Write necessario per la gestione degli accessi alla lista */
int syncFlag = 0;
int debug = 0;                              /* Se l'utente ha avviato in modalità debug, verranno mostrate informazioni aggiuntive */

char serverFileName[256];

ClientNode *clientList = NULL;
int clientListSize = 0;
int maxSockFd = 0;

// Main - Server 
int main(int argc, char *argv[])
{
    /* Inizializzazione sessione server */
    setbuf(stdout,NULL);
    srand(time(0));
    system("clear");

    /* Gestione del SIGINT (Ctrl+C) */
    //signal(SIGINT, ctrl_c_handler);

    /* Inizializzazione semaforo R/W */
    if(pthread_rwlock_init(&lockList, NULL) != 0) {
        printf("Failed semaphore initialization.\n");
        exit(-1);
    }
    
    /* Parse dei parametri passati da riga di comando */
    int port = parseCmdLine(argc, argv, "server", NULL, &debug);
    if(port == -1)
    {
        exit(0);
    }

    strcpy(serverFileName, argv[0]+2);

    /* Scrive in 'filesList' la lista dei file nella directory eccetto il file eseguibile del server */
    /*
    char listFileCmd[500];
    FILE *fp;
    int status;
    char path[512];
    char filesList[4000] = "\0";

    sprintf(listFileCmd, "ls -p | grep -v / | grep -v \"%s\"", argv[0]+2);

    fp = popen(listFileCmd, "r");
    if (fp == NULL){
        printf("PICCO\n");
        exit(-1);
    }


    while (fgets(path, 512, fp) != NULL)
        strcat(filesList, path);

    printf("\n\nLista dei files:\n%s",filesList);

    status = pclose(fp);
    if (status == -1) {
        printf("PICCO2\n");
        exit(-1);
    }
    */
    
    /* Dichiarazione variabili locali Main */
    int mainSockFd;     /*  */
    int ret;            /*  */
    int exist = 0;      /*  */

    pthread_t tid;      /*  */

    ClientNode *client; /*  */

    /* Sockaddr_in server */
    Sockaddr_in serverSocket;
    bzero(&serverSocket, sizeof(Sockaddr_in));
    serverSocket.sin_family = AF_INET;
    serverSocket.sin_addr.s_addr = htonl(INADDR_ANY);
    serverSocket.sin_port = htons(port);
    int addrlenServer = sizeof(serverSocket);

    /* Sockaddr_in client */
    Sockaddr_in clientSocket;
    bzero(&clientSocket, sizeof(Sockaddr_in));
    int addrlenClient = sizeof(clientSocket);

    /* Struct Segment di ricezione */
    Segment *rcvSegment = (Segment*)malloc(sizeof(Segment));
	if(rcvSegment == NULL)
	{
		printf("Error while trying to \"malloc\" a new Segment!\nClosing...\n");
		exit(-1);
	} 

	/*  */
    ThreadArgs *threadArgs = (ThreadArgs *)malloc(sizeof(ThreadArgs));
	if(threadArgs == NULL)
	{
		printf("Error while trying to \"malloc\" a new ThreadArgs!\nClosing...\n");
		exit(-1);
	}

    /* Socket - UDP */
    mainSockFd = socket(AF_INET, SOCK_DGRAM, IP_PROTOCOL);
    if (mainSockFd < 0){
        printf("\nFile descriptor not received!\n");
        exit(-1);
    }
    else
        printf("\nFile descriptor %d received!\n", mainSockFd);

    /* Assegnazione dell'indirizzo locale alla socket */
    if (bind(mainSockFd, (struct sockaddr*)&serverSocket, addrlenServer) == 0)
        printf("\nSuccessfully binded!\n");
    else {
        printf("\nBinding Failed!\n");
        exit(-1);
    }
	char *files;
    /* Server in attesa di richieste da parte dei client */
    while(1) {
        bzero(rcvSegment, sizeof(Segment));
        bzero(threadArgs, sizeof(ThreadArgs));

        recvSegment(mainSockFd, rcvSegment, &clientSocket, &addrlenClient);

        printf("[PKT]: Received packet from (%s:%d)\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));

    	/* Controllo se il Client esiste: in caso affermativo esegui l'operazione richiesta altrimenti utilizza un thread per la fase di 3-way handshake */
        pthread_rwlock_rdlock(&lockList);
        client = clientList;
        exist = 0;
        while(client != NULL) {
            if(client -> clientPort == ntohs(clientSocket.sin_port)) {
                // Client esiste
                exist = 1;
                break;
            }
            client = client -> next;
        }
        pthread_rwlock_unlock(&lockList);

        if(exist) {
    		/* Controlliamo se è un ACK+messaggio del SYN-ACK */
    		if((atoi(rcvSegment -> ackBit) == 1) && (atoi(rcvSegment -> ackNum) == (client -> lastSeqServer + 1))) {
        		/* Terminazione thread handshake */
    			pthread_cancel(client -> tid);
				pthread_join(client -> tid, NULL);
                close(client -> sockfd);
    		}
    		
    		/* Switch operazione richiesta */
    		switch(atoi(rcvSegment -> cmdType)) {

    			/* list */
    			case 1:
                    files = invokeFileList(argv[0] + 2);
                    printf("FILES:\n%s", files);

                    Segment *sendFileList = mallocSegment("1", EMPTY, FALSE, FALSE, FALSE, "1", files);
                    sendto(mainSockFd, sendFileList, sizeof(Segment), 0, (struct sockaddr*)&clientSocket, addrlenClient);
    				break;

    			/* download */
    			case 2:
                    printf("\nDownload request...\n");
                    threadArgs = newThreadArgs(clientSocket, *rcvSegment, client);
                    printf("\nSto nel main thread case 2: %d | (%s:%d)\n", threadArgs -> client -> sockfd, threadArgs -> client -> ip, threadArgs -> client -> clientPort);                    

                    /* Creazione di un thread utile alla fase di download */
                    ret = pthread_create(&tid, NULL, client_thread_download, (void *)threadArgs);
                    if(ret != 0)
                    {
                        printf("New client thread error\n");
                        exit(-1);
                    }

    				break;

    			/* upload */
    			case 3:
    				break;

    			/* Errore */
    			default:
    				printf("Error: wrong rcvSegment received from (%s:%d)\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));
        			exit(-1);
    				break;
    		}
        }
        /* Il client non esiste, creo una nuova istanza con fase di 3-way handshake*/
        else {
        	threadArgs = newThreadArgs(clientSocket, *rcvSegment, NULL);

            /* Creazione di un thread utile alla fase di handshake */
            ret = pthread_create(&tid, NULL, client_thread_handshake, (void *)threadArgs);
            if(ret != 0)
            {
                printf("New client thread error\n");
                exit(-1);
            }

            /* Attesa dell'aggiunta del nuovo client */
            while(syncFlag == 0);
            syncFlag = 0;
        }
    } 

    /* Distruzione semaforo R/W */
    if(pthread_rwlock_destroy(&lockList) != 0) {
        printf("Failed semaphore destruction.\n");
        exit(-1);
    }
    return 0; 
}

void ctrl_c_handler()
{
    printf("\n\n---> STAMPA LISTA <---\n\n");
    printf("|Size: %d - Max: %d|\n", clientListSize, maxSockFd);
    printList(clientList);

    int sock;
    printf("Socket da eliminare: ");
    scanf("%d", &sock);

    ClientNode *tmp = clientList;

    while(tmp != NULL)
    {
        if(tmp -> sockfd == sock)
            deleteClientNode(&clientList, tmp, &clientListSize, &maxSockFd);

        tmp = tmp -> next;
    }

    printf("\n\n---> STAMPA LISTA <---\n\n");
    printf("|Size: %d - Max: %d|\n", clientListSize, maxSockFd);
    printList(clientList);
}

/* Thread associato al client */
void *client_thread_handshake(void *args)
{		
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    int clientSockFd;
    int ret;

    ThreadArgs *threadArgs = (ThreadArgs*)args;
    printf("\n[TEST]: threadArgs for new client (%s:%d)!\n", inet_ntoa(threadArgs -> clientSocket.sin_addr), ntohs(threadArgs -> clientSocket.sin_port));

    /* Sockaddr_in client */
    Sockaddr_in clientSocket;
    clientSocket = threadArgs -> clientSocket;
    int addrlenClient = sizeof(clientSocket);

    /* Sockaddr_in server */
    Sockaddr_in serverSocket;
    serverSocket.sin_family = AF_INET;
    serverSocket.sin_addr.s_addr = inet_addr(inet_ntoa(clientSocket.sin_addr));
    int addrlenServer = sizeof(serverSocket);

    /* Socket UDP */
    clientSockFd = socket(AF_INET, SOCK_DGRAM, IP_PROTOCOL);
    if (clientSockFd < 0){
        printf("\nFailed creating socket for new client (%s:%d)!\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));
        exit(-1);
    }

    do {
        serverSocket.sin_port = htons(1024 + rand() % (65535+1 - 1024));
    } while(bind(clientSockFd, (struct sockaddr*)&serverSocket, addrlenServer) != 0);
    
    ClientNode *newClient = newNode(clientSockFd, inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port), pthread_self(), ntohs(serverSocket.sin_port), (threadArgs -> segment).seqNum); 

    pthread_rwlock_wrlock(&lockList);
    addClientNode(&clientList, newClient, &clientListSize, &maxSockFd);
    pthread_rwlock_unlock(&lockList);
    syncFlag = 1;

    printf("\n\nThread created for (%s:%d), bind on %d\n", newClient -> ip, newClient -> clientPort, newClient -> serverPort);

    /* SYN-ACK */
    char ackNum[MAX_SEQ_ACK_NUM];
    sprintf(ackNum, "%d", atoi((threadArgs -> segment).seqNum) + 1);

    Segment *synAck = mallocSegment("1", ackNum, TRUE, TRUE, FALSE, EMPTY, EMPTY);

    /* Invio SYN-ACK */
    if((ret = sendto(clientSockFd, synAck, sizeof(Segment), 0, (struct sockaddr*)&clientSocket, addrlenClient)) != sizeof(Segment)) {
        printf("Error trying to send a SYN-ACK segment to client (%s:%d)\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));
        exit(-1);
    }
    else
	   printf("SYN-ACK sent to the client (%s:%d)\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));
	newClient -> lastSeqServer = 1;

    /* ACK del SYN-ACK */
    Segment *rcvSegment = (Segment*) malloc(sizeof(Segment));
    if(rcvSegment == NULL)
    {
        printf("Error while trying to \"malloc\" a new ACK segment of SYN-ACK!\nClosing...\n");
        exit(-1);
    }
    bzero(rcvSegment, sizeof(Segment));
    
    recvSegment(clientSockFd, rcvSegment, &clientSocket, &addrlenClient);

    printf("\n[PKT_HANDSHAKE_THREAD]: Received packet from (%s:%d)\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));
    
    newClient -> lastSeqClient = atoi(rcvSegment -> seqNum);

    printf("\nHandshake terminated with client (%s:%d)\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));

    close(clientSockFd);
    pthread_exit(NULL);

    /* 
	 *
	 *
	 * Timeout syn-ack: se dopo X secondi/minuti non ho ricevuto 
	 *
	 *
    */
}

/* Thread associato al client */
void *client_thread_list(void *args)
{       
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    //close(clientSockFd);
    pthread_exit(NULL);
}

/* Thread associato al client */
void *client_thread_download(void *args)
{       
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    int clientSockFd;
    int ret;

    ThreadArgs *threadArgs = (ThreadArgs*)args;

    /* Sockaddr_in client */
    Sockaddr_in clientSocket;
    clientSocket = threadArgs -> clientSocket;
    int addrlenClient = sizeof(clientSocket);

    /* Sockaddr_in server */
    Sockaddr_in serverSocket;
    serverSocket.sin_family = AF_INET;
    serverSocket.sin_addr.s_addr = inet_addr(inet_ntoa(clientSocket.sin_addr));
    int addrlenServer = sizeof(serverSocket);

    /* Socket UDP */
    clientSockFd = socket(AF_INET, SOCK_DGRAM, IP_PROTOCOL);
    if (clientSockFd < 0){
        printf("\nFailed creating socket for client download (%s:%d)!\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));
        exit(-1);
    }

    do {
        serverSocket.sin_port = htons(1024 + rand() % (65535+1 - 1024));
    } while(bind(clientSockFd, (struct sockaddr*)&serverSocket, addrlenServer) != 0);

    printf("\n[TEST]: threadArgs for client download (%s:%d)!\n", inet_ntoa(threadArgs -> clientSocket.sin_addr), ntohs(threadArgs -> clientSocket.sin_port));


    //printf("\n\nThread for download for (%s:%d), bind on %d\n", threadArgs -> client -> ip, threadArgs -> client -> clientPort, serverSocket.sin_port);

    // /* Se il file è presente nel server */
    // if(fileExist(serverFileName, (threadArgs -> segment).msg)) {

    //     // Aprire il file da inviare e scriverlo dentro sndSegment...
    //     FILE *file = fopen((threadArgs -> segment).msg, "r");
    //     char buffFile[4081];

    //     fscanf(file,"%4080[^\n]",buffFile);
    //     printf("\nFile: \n%s", buffFile);

    //      ACK+DATI della richiesta di download da parte del client 
    //     char ackNum[MAX_SEQ_ACK_NUM];
    //     sprintf(ackNum, "%d", atoi((threadArgs -> segment).seqNum) + 1);
    //     Segment *sndSegment = mallocSegment("1", ackNum, FALSE, TRUE, FALSE, "2", buffFile);
    //     //sendto(clientSockFd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&clientSocket, addrlenClient);
    // }
    // else {

    // }


    close(clientSockFd);
    pthread_exit(NULL);
}

/* Thread associato al client */
void *client_thread_upload(void *args)
{       
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    //close(clientSockFd);
    pthread_exit(NULL);
}