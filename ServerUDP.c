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
#include "Server.h"

#define IP_PROTOCOL 0
#define PORT 5555
#define NET_BUF_SIZE 32
#define NOFILE "File Not Found!"

/* Prototipi */
void *client_thread_handshake(void *);                /* Thread associato al client */
void ctrl_c_handler();

/* Variabili globali */
pthread_rwlock_t lockList;                  /* Semaforo Read/Write necessario per la gestione degli accessi alla lista */
int syncFlag = 0;

int len;

ClientNode *clientList = NULL;
int clientListSize = 0;
int maxSockFd = 0;

// Main - Server 
int main(int argc, char *argv[])
{ 
    system("clear");
    setbuf(stdout,NULL);
    srand(time(0));
    signal(SIGINT, ctrl_c_handler);

    /* Inizializzazione semaforo R/W */
    if(pthread_rwlock_init(&lockList, NULL) != 0) {
        printf("Failed semaphore initialization.\n");
        exit(-1);
    }

    int mainSockFd;
  
    int ret;

    int exist = 0;

    pthread_t tid;

    ClientNode *tmp;

    /* Struct sockaddr_in server */
    struct sockaddr_in serverSocket;
    serverSocket.sin_family = AF_INET;
    serverSocket.sin_port = htons(PORT);
    serverSocket.sin_addr.s_addr = INADDR_ANY;
    int addrlenServer = sizeof(serverSocket);

    /* Struct sockaddr_in client */
    struct sockaddr_in clientSocket;
    int addrlenClient = sizeof(clientSocket);

    Segment *rcvSegment = (Segment*)malloc(sizeof(Segment));
	if(rcvSegment == NULL)
	{
		printf("Error while trying to \"malloc\" a new Segment!\nClosing...\n");
		exit(-1);
	} 

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

    /* Server in attesa di richieste da parte dei client */
    while (1) {

        printf("\nWaiting for operation request...\n");
  
        /* Receive file name */
        bzero(rcvSegment, sizeof(Segment));
        bzero(threadArgs, sizeof(ThreadArgs));

        printf("Printf a caso\n");

        /* Descrittore IP:Porta */
        recvfrom(mainSockFd, rcvSegment, sizeof(Segment), 0, (struct sockaddr*)&clientSocket, (socklen_t*)&addrlenClient);

        /* Controllo se il Client esiste: in caso affermativo esegui l'operazione richiesta altrimenti utilizza un thread per la fase di 3-way handshake */
        tmp = clientList;
        exist = 0;
        while(tmp != NULL) {
            if(tmp -> clientPort == ntohs(clientSocket.sin_port)) {
                // Client esiste
                exist = 1;
                break;
            }
            tmp = tmp -> next;
        }

        if(exist) {
            // switch-case operazione e tira su il thread corrispondente all' operazione
        } 
        else {
        	threadArgs = newThreadArgs(clientSocket, rcvSegment -> seqNum);

            /* Creazione di un thread utile alla fase di handshake */
            ret = pthread_create(&tid, NULL, client_thread_handshake, (void *)threadArgs);
            if(ret != 0)
            {
                printf("New client thread error\n");
                exit(-1);
            }
        }

        //printf("\n\nReceived packet from %d:%s:%d\n", newClient->sockfd, newClient->ip, newClient->clientPort);
        //printf("\nClient says: %s", net_buf);

        while(syncFlag == 0);
        pthread_rwlock_rdlock(&lockList);
        printf("\n\n---> STAMPA LISTA <---\n\n");
        printf("|Size: %d - MAx: %d|\n", clientListSize, maxSockFd);
        printList(clientList);
        pthread_rwlock_unlock(&lockList);
        syncFlag = 0;

        //bzero(net_buf, NET_BUF_SIZE);
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
    printf("|Size: %d - MAx: %d|\n", clientListSize, maxSockFd);
    printList(clientList);
}

/* Thread associato al client */
void *client_thread_handshake(void *args)
{
    int clientSockFd;
    int ret;

    ThreadArgs *threadArgs = (ThreadArgs*)args;

    /* Struct sockaddr_in server */
    struct sockaddr_in serverSocket;
    serverSocket.sin_family = AF_INET;
    serverSocket.sin_addr.s_addr = inet_addr(inet_ntoa(threadArgs -> clientSocket.sin_addr));
    int addrlenServer = sizeof(serverSocket);

    clientSockFd = socket(AF_INET, SOCK_DGRAM, IP_PROTOCOL);

    if (clientSockFd < 0){
        printf("\nFailed creating socket for new client (%s:%d)!\n", inet_ntoa(threadArgs -> clientSocket.sin_addr), ntohs(threadArgs -> clientSocket.sin_port));
        exit(-1);
    }

    do {
        serverSocket.sin_port = 1024 + rand() % (65535+1 - 1024);
    } while(bind(clientSockFd, (struct sockaddr*)&serverSocket, addrlenServer) != 0);

    ClientNode *newClient = newNode(clientSockFd, inet_ntoa(threadArgs -> clientSocket.sin_addr), ntohs(threadArgs -> clientSocket.sin_port), pthread_self(), serverSocket.sin_port, threadArgs -> seqNumClient);
    
    pthread_rwlock_wrlock(&lockList);
    syncFlag = 1;
    addClientNode(&clientList, newClient, &clientListSize, &maxSockFd);
    pthread_rwlock_unlock(&lockList);

    printf("\n\nThread creato per %s:%d, bind su %d\n\n", newClient -> ip, newClient -> clientPort, newClient -> serverPort);


    /* SYN-ACK */
    char ackNum[MAX_SEQ_ACK_NUM];
    sprintf(ackNum, "%d", atoi(threadArgs -> seqNumClient) + 1);

    Segment *synAck = newSegment("0", ackNum, TRUE, TRUE, FALSE, "");

	if((ret = sendto(clientSockFd, synAck, sizeof(Segment), 0, (struct sockaddr*)&serverSocket, addrlenServer)) != sizeof(Segment)) {
        printf("Error trying to send a SYN-ACK segment to client %s:%d\n", inet_ntoa(threadArgs -> clientSocket.sin_addr), ntohs(threadArgs -> clientSocket.sin_port));
        exit(-1);
    }
    newClient -> lastSeqServer = 0;

    Segment *rcvSegment = (Segment*) malloc(sizeof(Segment));
    if(rcvSegment == NULL)
    {
        printf("Error while trying to \"malloc\" a new ACK segment of SYN-ACK!\nClosing...\n");
        exit(-1);
    }
    recvfrom(clientSockFd, rcvSegment, sizeof(Segment), 0, NULL, NULL);
    if((atoi(rcvSegment -> ackBit) != 1) || (atoi(rcvSegment -> ackNum) != (newClient -> lastSeqServer + 1))) {
        printf("Error: wrong ACK of SYN-ACK received from %s:%d\n", inet_ntoa(threadArgs -> clientSocket.sin_addr), ntohs(threadArgs -> clientSocket.sin_port));
        exit(-1);
    }
    close(clientSockFd);
    pthread_exit(NULL);
    // Timeout syn-ack: se dopo X secondi/minuti non ho ricevuto 



    // /* Struct sockaddr_in server */
    // serverSocket.sin_addr.s_addr = inet_addr(inet_ntoa(clientSocket -> sin_addr)); 
    // addrlenServer = sizeof(serverSocket);

    // char net_buf[NET_BUF_SIZE];

    // printf("\n\nReceived packet from %s:%d\n", inet_ntoa(clientSocket -> sin_addr), ntohs(clientSocket -> sin_port));

    // clientSockFd = socket(AF_INET, SOCK_DGRAM, IP_PROTOCOL); 

    // /* Assegnazione dell'indirizzo locale alla socket */
    // if (bind(clientSockFd, (struct sockaddr*)&serverSocket, addrlenServer) == 0) 
    //     printf("\nSuccessfully thread binded!\n"); 
    // else
    //     printf("\nBinding thread failed!\n"); 

    // sendto(clientSockFd, "SONO IL SERVER prima del while", NET_BUF_SIZE, 0, (struct sockaddr*)&serverSocket, addrlenServer);

    // while(1) {

    //     printf("\nThread while(1) \n");
    //     // genera porta e verifica che non è in uso

    //     //sendto(clientSockFd, "SONO IL SERVER dopo il while", NET_BUF_SIZE, 0, (struct sockaddr*)&serverSocket, addrlenServer);

    //     printf("\nSendto executed\n");
    //     fflush(stdout);

    //     recvfrom(clientSockFd, net_buf, NET_BUF_SIZE, 0, (struct sockaddr*)&clientSocket, (socklen_t*)&addrlenClient); 
    //     printf("\n\nReceived packet from %s:%d\n", inet_ntoa(clientSocket -> sin_addr), ntohs(clientSocket -> sin_port));
    //     printf("Received: %s\n", net_buf);
    //     bzero(net_buf, NET_BUF_SIZE);
    // }

    pthread_exit(NULL);
}