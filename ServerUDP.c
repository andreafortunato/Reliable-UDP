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
#include <errno.h>
#include "Server.h"

#define IP_PROTOCOL 0

/* Prototipi */
void *client_thread_handshake(void *);      /* Thread associato al client */
void *client_thread_list(void *);
void list(ClientNode *client);
void download(ClientNode *client, char *fileName);
void fin(ClientNode *client);
void *client_thread_download(void *);
void *client_thread_upload(void *);
void ctrl_c_handler();

/* Variabili globali */
pthread_rwlock_t lockList;                  /* Semaforo Read/Write necessario per la gestione degli accessi alla lista */
int syncFlag = 0;
int debug = 0;                              /* Se l'utente ha avviato in modalità debug, verranno mostrate informazioni aggiuntive */

char **fileNameList;
int numFiles = 0;

ClientNode *clientList = NULL;
int clientListSize = 0;
int maxSockFd = 0;

int maxSeqNum;

// Main - Server 
int main(int argc, char *argv[])
{
    /* Inizializzazione sessione server */
    setbuf(stdout,NULL);
    srand(time(0));
    system("clear");

    /* Gestione del SIGINT (Ctrl+C) */
    signal(SIGINT, ctrl_c_handler);

    /* Inizializzazione semaforo R/W */
    if(pthread_rwlock_init(&lockList, NULL) != 0) {
        printf("Failed semaphore initialization.\n");
        exit(-1);
    }
    
    /* Parse dei parametri passati da riga di comando */
    // int port = parseCmdLine(argc, argv, "server", NULL, &debug);
    // if(port == -1)
    // {
    //     exit(0);
    // }

    maxSeqNum = WIN_SIZE*2;

    fileNameList = getFileNameList(argv[0]+2, &numFiles);

    /* Dichiarazione variabili locali Main */
    int mainSockFd;     /*  */
    int ret;            /*  */

    pthread_t tid;      /*  */

    /* Sockaddr_in server */
    Sockaddr_in serverSocket;
    bzero(&serverSocket, sizeof(Sockaddr_in));
    serverSocket.sin_family = AF_INET;
    serverSocket.sin_addr.s_addr = htonl(INADDR_ANY);
    //serverSocket.sin_port = htons(port);
    serverSocket.sin_port = htons(47435);
    int addrlenServer = sizeof(serverSocket);

    /* Sockaddr_in client */
    Sockaddr_in clientSocket;
    bzero(&clientSocket, sizeof(Sockaddr_in));
    int addrlenClient = sizeof(clientSocket);

    /* Struct Segment di ricezione */
    Segment *rcvSegment = (Segment*)malloc(sizeof(Segment));
	if(rcvSegment == NULL)
	{
		printf("Error while trying to \"malloc\" a new rcvSegment!\nClosing...\n");
		exit(-1);
	} 

	/* Struttura threadArgs per parametri del thread */
    ThreadArgs *threadArgs = (ThreadArgs*)malloc(sizeof(ThreadArgs));
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

    /* Assegnazione dell'indirizzo locale alla socket */
    if (bind(mainSockFd, (struct sockaddr*)&serverSocket, addrlenServer) == 0) {
        printf("\nServer online...\n\n");
    }
    else {
        printf("\nBinding Failed!\n");
        exit(-1);
    }

    /* Configurazione dimensione buffer di ricezione */
    // int sockBufLen = SOCKBUFLEN;
    // if (setsockopt(mainSockFd, SOL_SOCKET, SO_RCVBUFFORCE, &sockBufLen, sizeof(int)) == -1) {
    //     printf("Error while setting SO_RCVBUF for socket %d: %s\n", mainSockFd, strerror(errno));
    //     exit(-1);
    // }

    /* Server in attesa di richieste da parte dei client */
    while(1) {
        bzero(rcvSegment, sizeof(Segment));
        bzero(threadArgs, sizeof(ThreadArgs));

        recvSegment(mainSockFd, rcvSegment, &clientSocket, &addrlenClient);
        printf("\n[PKT-MAIN]: Received packet from (%s:%d)\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));
        threadArgs = newThreadArgs(clientSocket, *rcvSegment);

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

    /* Distruzione semaforo R/W */
    if(pthread_rwlock_destroy(&lockList) != 0) {
        printf("Failed semaphore destruction.\n");
        exit(-1);
    }
    return 0; 
}

void ctrl_c_handler()
{
    printf("\n\n---> STAMPA LISTA FILE <---\n\n");
    printf("Number of files: %d\n", numFiles);
    printf("%s", fileNameListToString(fileNameList, numFiles));
    
    printf("\n\n---> STAMPA LISTA <---\n\n");
    printf("|Size: %d - Max: %d|\n", clientListSize, maxSockFd);
    printList(clientList);

    // int sock;
    // printf("\nSocket da eliminare: ");
    // scanf("%d", &sock);

    // ClientNode *tmp = clientList;

    // while(tmp != NULL)
    // {
    //     if(tmp -> sockfd == sock)
    //         deleteClientNode(&clientList, tmp, &clientListSize, &maxSockFd);

    //     tmp = tmp -> next;
    // }

    // printf("\n\n---> STAMPA LISTA <---\n\n");
    // printf("|Size: %d - Max: %d|\n", clientListSize, maxSockFd);
    // printList(clientList);
}

/* Thread associato al client */
void *client_thread_handshake(void *args)
{		
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    int clientSockFd;
    int ret;

    int *tmpIntBuff;
    char *tmpStrBuff;

    ThreadArgs *threadArgs = (ThreadArgs*)args;
    printf("\n[TEST]: threadArgs for new client (%s:%d)!\n", inet_ntoa(threadArgs -> clientSocket.sin_addr), ntohs(threadArgs -> clientSocket.sin_port));

    int ackNum = atoi((threadArgs -> segment).seqNum) + 1;

    /* Sockaddr_in client */
    Sockaddr_in clientSocket;
    clientSocket = threadArgs -> clientSocket;
    int addrlenClient = sizeof(clientSocket);

    syncFlag = 1;

    printf("\n[TEST]: threadArgs for client handshake (%s:%d)!\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));

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
        serverSocket.sin_port = htons(49152 + rand() % (65535+1 - 49152));
    } while(bind(clientSockFd, (struct sockaddr*)&serverSocket, addrlenServer) != 0);
    
    ClientNode *newClient = newNode(clientSockFd, inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port), clientSocket, pthread_self(), ntohs(serverSocket.sin_port), (threadArgs -> segment).seqNum); 

    pthread_rwlock_wrlock(&lockList);
    addClientNode(&clientList, newClient, &clientListSize, &maxSockFd);
    
    printf("New Client\n");
        if(newClient->prev)
            printf("Prev: %d\n", newClient->prev->clientPort);
        else
            printf("Prev: NULL\n");

        printf("Sockfd: %d\nIP: %s\nPort: %d\n", newClient->sockfd, newClient->ip, newClient->clientPort);
        
        if(newClient->next)
            printf("Next: %d\n", newClient->next->clientPort);
        else
            printf("Next: NULL\n");


    pthread_rwlock_unlock(&lockList);

    printf("\n\nThread created for (%s:%d), bind on %d\n", newClient -> ip, newClient -> clientPort, newClient -> serverPort);

    /* SYN-ACK */
    tmpIntBuff = strToInt(EMPTY);
    Segment *synAck = NULL;
    newSegment(&synAck, FALSE, 1, ackNum, TRUE, TRUE, FALSE, EMPTY, 1, tmpIntBuff);

    /* Invio SYN-ACK */
    if((ret = sendto(clientSockFd, synAck, sizeof(Segment), 0, (struct sockaddr*)&clientSocket, addrlenClient)) != sizeof(Segment)) {
        printf("Error trying to send a SYN-ACK segment to client (%s:%d)\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));
        exit(-1);
    }
    else
	   printf("SYN-ACK sent to the client (%s:%d)\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));

	newClient -> lastSeqServer = 1;
    free(synAck);
    free(tmpIntBuff);

    /* ACK del SYN-ACK */
    Segment *rcvSegment = (Segment*) malloc(sizeof(Segment));
    if(rcvSegment == NULL)
    {
        printf("Error while trying to \"malloc\" a new ACK segment of SYN-ACK!\nClosing...\n");
        exit(-1);
    }

    recvSegment(clientSockFd, rcvSegment, &clientSocket, &addrlenClient);
    tmpStrBuff = intToStr(rcvSegment -> msg, atoi(rcvSegment -> lenMsg));
    if(strcmp(tmpStrBuff, EMPTY) == 0) {
        recvSegment(clientSockFd, rcvSegment, &clientSocket, &addrlenClient);
        switch(atoi(rcvSegment -> cmdType)) {

            case 1:
                list(newClient);
                break;

            case 2:
                tmpStrBuff = intToStr(rcvSegment -> msg, atoi(rcvSegment -> lenMsg));
                printf("FILE RICHIESTO (%s)\n", tmpStrBuff);
                download(newClient, tmpStrBuff);
                free(tmpStrBuff);
                break;

            case 3:
                break; 

        }
    }

    free(rcvSegment);

    pthread_exit(NULL);

    /* 
	 *
	 *
	 * Timeout syn-ack: se dopo X secondi/minuti non ho ricevuto 
	 *
	 *
    */
}

void list(ClientNode *client) {

    int addrlenClient = sizeof(client -> connection);
    int totalSegs;
    int i,j;
    int lenBuff;
    int currentPos = 1;
    char *tmpStrBuff;
    char *filelist;
    int tmpIntBuff[LEN_MSG];

    Segment *sndSegment = NULL;

    Segment *rcvSegment = (Segment*) malloc(sizeof(Segment));
    if(rcvSegment == NULL)
    {
        printf("Error while trying to \"malloc\" a new segment for list operation!\nClosing...\n");
        exit(-1);
    }

    /* Invio della lista dei file al Client */
    filelist = fileNameListToString(fileNameList, numFiles);
    lenBuff = strlen(filelist);
    
    totalSegs = (lenBuff-1)/(LEN_MSG) + 1; /* Formula della vita: (A-1)/B+1, parte intera superiore della divisione A/B */

    for(i=0; i<totalSegs; i++) {
        bzero(tmpIntBuff, sizeof(int)*LEN_MSG);

        /* */
        for(j = 1; (j <= LEN_MSG) && (currentPos < lenBuff); j++, currentPos++) {
            tmpIntBuff[j-1] = htonl((int)filelist[currentPos-1]);
        }

        if(i == totalSegs-1)
            newSegment(&sndSegment, TRUE, (i%maxSeqNum)+1, -1, FALSE, FALSE, FALSE, "1", j-1, tmpIntBuff);
        else
            newSegment(&sndSegment, FALSE, (i%maxSeqNum)+1, -1, FALSE, FALSE, FALSE, "1", j-1, tmpIntBuff);

        sendto(client -> sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&(client -> connection), addrlenClient);
        tmpStrBuff = intToStr(tmpIntBuff, j-1);
        printf("\nInvio pacchetto - (seqNum: %d) - (msg: %s)\n", atoi(sndSegment -> seqNum), tmpStrBuff);
        bzero(tmpStrBuff, strlen(tmpStrBuff));
        recvSegment(client -> sockfd, rcvSegment, &(client -> connection), &addrlenClient);
        printf("Ricevuto pacchetto - (ackNum: %d)\n", atoi(rcvSegment -> ackNum));
    }

    free(tmpStrBuff);
    free(sndSegment);
    free(rcvSegment);

    /* Fase di FIN */
    fin(client);
}

/* Funzione download */
void download(ClientNode *client, char *fileName)
{    
    Segment *sndSegment = NULL;
    Segment *rcvSegment = (Segment*) malloc(sizeof(Segment));
    if(rcvSegment == NULL) {
        printf("Error while trying to \"malloc\" a new download rcvSegment of (%s:%d)!\nClosing...\n", client -> ip, client -> clientPort);
        exit(-1);
    }

    int addrlenClient = sizeof(client -> connection);

    printf("\nFile richiesto: %s\n", fileName);

    /* Se il file è presente nel server */
    if(fileExist(fileNameList, fileName, numFiles)) {

        // Aprire il file da inviare e scriverlo dentro sndSegment...
        FILE *file = fopen(fileName, "rb");

        char b[1024];
        getcwd(b, 1024);
        printf("%s", b);

        fseek(file, 0, SEEK_END);
        int fileLen = ftell(file);
        fseek(file, 0, SEEK_SET);

        int totalSegs = (fileLen-1)/(LEN_MSG) + 1; /* Formula della vita: (A-1)/B+1, parte intera superiore della divisione A/B */
        printf("\nTotalsegs: %d - fileLen: %d\n", totalSegs, fileLen);

        int buffFile[LEN_MSG];
        int ch;
        int i, j;
        int currentPos = 1; 

        for(i=0; i<totalSegs; i++) {
           
            bzero(buffFile, LEN_MSG);
            for(j = 1; j <= LEN_MSG; j++, currentPos++) {
                if((ch = fgetc(file)) == EOF)
                    break;
                buffFile[j-1] = htonl(ch);
            }

            if(i == totalSegs-1)
                newSegment(&sndSegment, TRUE, (i%maxSeqNum)+1, -1, FALSE, FALSE, FALSE, "1", j-1, buffFile);
            else
                newSegment(&sndSegment, FALSE, (i%maxSeqNum)+1, -1, FALSE, FALSE, FALSE, "1", j-1, buffFile);

            /* Tenta invio */
            if((rand()%100 + 1) > LOSS_PROB) {
                //sendto(client -> sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&(client -> connection), addrlenClient);
            }
            sendto(client -> sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&(client -> connection), addrlenClient);
            //printf("\nInvio pacchetto - (seqNum: %d)\n", atoi(sndSegment -> seqNum));
            recvSegment(client -> sockfd, rcvSegment, &(client -> connection), &addrlenClient);
            //printf("Ricevuto pacchetto - (ackNum: %d)\n", atoi(rcvSegment -> ackNum));
        }

        printf("Fine invio pacchetti\n");
        fclose(file);
    }
    else {
        printf("\nFile not found!\n");
    }

    free(sndSegment);
    free(rcvSegment);

    fin(client);
}

/* Funzione per la chiusura della connessione */
void fin(ClientNode *client) {

    int addrlenClient = sizeof(client -> connection);

    int ackNum;
    int *tmpIntBuff;

    Segment *sndSegment = NULL;

    Segment *rcvSegment = (Segment*) malloc(sizeof(Segment));
    if(rcvSegment == NULL)
    {
        printf("Error while trying to \"malloc\" a new segment for FIN operation!\nClosing...\n");
        exit(-1);
    }

    /* Ricezione FIN */
    recvSegment(client -> sockfd, rcvSegment, &(client -> connection), &addrlenClient);

    /* Invio ACK del FIN */
    ackNum = atoi(rcvSegment -> seqNum) + 1;
    tmpIntBuff = strToInt(EMPTY);
    newSegment(&sndSegment, FALSE, 2, ackNum, FALSE, TRUE, FALSE, EMPTY, 1, tmpIntBuff);
    sendto(client -> sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&(client -> connection), addrlenClient);

    /* Invio FIN */
    newSegment(&sndSegment, FALSE, 2, ackNum, FALSE, TRUE, TRUE, EMPTY, 1, tmpIntBuff);
    sendto(client -> sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&(client -> connection), addrlenClient);

    /* Ricezione ACK del FIN */
    recvSegment(client -> sockfd, rcvSegment, &(client -> connection), &addrlenClient);

    free(tmpIntBuff);

    deleteClientNode(&clientList, client, &clientListSize, &maxSockFd);
    printf("\nTrasmissione terminata e disconnessione effettuata con successo!\n");
}

/* Thread associato al client */
void *client_thread_list(void *args)
{       
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    //close(clientSockFd);
    pthread_exit(NULL);
}

/* Thread associato al client */
void *client_thread_upload(void *args)
{       
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    //close(clientSockFd);
    pthread_exit(NULL);
}