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

struct sockaddr_in serverSocket;
int addrlenServer;
int len;

ClientNode *clientList = NULL;
int clientListSize = 0;
int maxSockFd = 0;

/* Prototipi */
void *client_thread(void *);                /* Thread associato al client */
void ctrl_c_handler();

// Main - Server 
int main(int argc, char *argv[])
{ 
    system("clear");
    setbuf(stdout,NULL);
    srand(time(0));
    signal(SIGINT, ctrl_c_handler);

    int mainSockFd;
    int nBytes;
  
    int ret;

    pthread_t tid;

    int randInt;

    ClientNode *tmp;

    /* Struct sockaddr_in server */
    serverSocket.sin_family = AF_INET;
    serverSocket.sin_port = htons(PORT);
    serverSocket.sin_addr.s_addr = INADDR_ANY;
    addrlenServer = sizeof(serverSocket);

    /* Struct sockaddr_in client */
    struct sockaddr_in clientSocket;
    int addrlenClient = sizeof(clientSocket);

    /* Buffer di rete */
    char net_buf[NET_BUF_SIZE];
    FILE *fp;
    
    system("clear");

    /* Socket - UDP */
    mainSockFd = socket(AF_INET, SOCK_DGRAM, IP_PROTOCOL);
    if (mainSockFd < 0)
        printf("\nFile descriptor not received!\n");
    else
        printf("\nFile descriptor %d received!\n", mainSockFd);

    /* Assegnazione dell'indirizzo locale alla socket */
    if (bind(mainSockFd, (struct sockaddr*)&serverSocket, addrlenServer) == 0)
        printf("\nSuccessfully binded!\n");
    else
        printf("\nBinding Failed!\n");

    /* Server in attesa di richieste da parte dei client */
    while (1) {

        printf("\nWaiting for operation request...\n");
  
        /* Receive file name */
        bzero(net_buf, NET_BUF_SIZE);

        /* Descrittore IP:Porta */
        recvfrom(mainSockFd, net_buf, NET_BUF_SIZE, 0, (struct sockaddr*)&clientSocket, &addrlenClient);

        // if(client esiste)
        // {
        //     tira su un thread ed esegui l'operazione richiesta
        // } else {
        //     tira su un thread e stabilisci la connessione (3-way handshake)
        // }


        tmp = clientList;
        while(tmp != NULL) {
            if(5 == ntohs(clientSocket.sin_port)) {
                // Client esiste
            } else {
                randInt = 5 + rand() % (1024+1 - 5);
                ClientNode *newClient = newNode(randInt, inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));
                addClientNode(&clientList, newClient, &clientListSize, &maxSockFd);

                ThreadArgs *threadArgs = newThreadArgs(&clientSocket, newClient);

                /* Creazione di un thread utile alla fase di handshake */
                ret = pthread_create(&tid, NULL, client_thread, (void *)threadArgs);
                if(ret != 0)
                {
                    printf("New client thread error\n");
                    exit(-1);
                }
            }
            tmp = tmp -> next;
        }

        printf("\n\nReceived packet from %d:%s:%d\n", newClient->sockfd, newClient->ip, newClient->port);
        printf("\nClient says: %s", net_buf);

        printf("\n\n---> STAMPA LISTA <---\n\n");
        printf("|Size: %d - MAx: %d|\n", clientListSize, maxSockFd);
        printList(clientList);

        bzero(net_buf, NET_BUF_SIZE);
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
void *client_thread(void *args)
{   
    int clientSockFd;

    struct sockaddr_in *clientSocket = (struct sockaddr_in*)args;
    int addrlenClient = sizeof(*clientSocket);

    ThreadArgs *threadArgs = (ThreadArgs*)args;
    threadArgs -> clientNode -> clientTid = gettid();


    /* Struct sockaddr_in server */
    serverSocket.sin_addr.s_addr = inet_addr(inet_ntoa(clientSocket -> sin_addr)); 
    addrlenServer = sizeof(serverSocket);

    char net_buf[NET_BUF_SIZE];

    printf("\n\nReceived packet from %s:%d\n", inet_ntoa(clientSocket -> sin_addr), ntohs(clientSocket -> sin_port));

    clientSockFd = socket(AF_INET, SOCK_DGRAM, IP_PROTOCOL); 

    /* Assegnazione dell'indirizzo locale alla socket */
    if (bind(clientSockFd, (struct sockaddr*)&serverSocket, addrlenServer) == 0) 
        printf("\nSuccessfully thread binded!\n"); 
    else
        printf("\nBinding thread failed!\n"); 

    sendto(clientSockFd, "SONO IL SERVER prima del while", NET_BUF_SIZE, 0, (struct sockaddr*)&serverSocket, addrlenServer);

    while(1) {

        printf("\nThread while(1) \n");
        // genera porta e verifica che non è in uso

        //sendto(clientSockFd, "SONO IL SERVER dopo il while", NET_BUF_SIZE, 0, (struct sockaddr*)&serverSocket, addrlenServer);

        printf("\nSendto executed\n");
        fflush(stdout);

        recvfrom(clientSockFd, net_buf, NET_BUF_SIZE, 0, (struct sockaddr*)&clientSocket, &addrlenClient); 
        printf("\n\nReceived packet from %s:%d\n", inet_ntoa(clientSocket -> sin_addr), ntohs(clientSocket -> sin_port));
        printf("Received: %s\n", net_buf);
        bzero(net_buf, NET_BUF_SIZE);
    }

    pthread_exit(NULL);
}