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
  
#define IP_PROTOCOL 0 
#define PORT 5555 
#define NET_BUF_SIZE 32  
#define NOFILE "File Not Found!" 

struct sockaddr_in serverSocket; 
int addrlenServer;
int len;

/* Prototipi */
void *client_thread(void *);                /* Thread associato al client */

// Main - Server 
int main(int argc, char *argv[]) 
{ 
    int mainSockFd; 
    int nBytes; 
  
    int ret;

    pthread_t tid;

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

        printf("\nWaiting for file name...\n"); 
  
        /* Receive file name */
        bzero(net_buf, NET_BUF_SIZE); 
  
        /* Descrittore IP:Porta */
        recvfrom(mainSockFd, net_buf, NET_BUF_SIZE, 0, (struct sockaddr*)&clientSocket, &addrlenClient); 
        printf("\n\nReceived packet from %s:%d\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));
        printf("\nClient says: %s", net_buf);

        bzero(net_buf, NET_BUF_SIZE);

        /* Creazione del thread associato al nuovo client */
        ret = pthread_create(&tid, NULL, client_thread, (void *)&clientSocket);
        if(ret != 0)
        {
            printf("New client thread error\n");
            exit(-1);
        }

        // /* Apertura del file "net_buf" in lettura */
        // fp = fopen(net_buf, "r"); 
        // printf("\nFile Name Received: %s\n", net_buf); 
        // if (fp == NULL) 
        //     printf("\nFile open failed!\n"); 
        // else
        //     printf("\nFile successfully opened!\n"); 

        // /* If file not found send "File Not Found!" to client */
        // if (fp == NULL) { 
        //     clearBuf(net_buf);
        //     strcpy(net_buf, NOFILE); 
        //     len = strlen(NOFILE); 
        //     net_buf[len] = EOF; 
        // } 
        // else { /* File exists, send file to client */

        //     char ch;

        //     clearBuf(net_buf);
        //     for (int i = 0; i < NET_BUF_SIZE; i++) { 
        //         ch = fgetc(fp); 
        //         net_buf[i] = ch;
        //         if (ch == EOF) 
        //             break; 
        //     }
        // }

        // /* Send file */
        // sendto(sockfd, net_buf, NET_BUF_SIZE, 0, (struct sockaddr*)&addr_con, addrlen); 

        // clearBuf(net_buf); 

        // if (fp != NULL) 
        //     fclose(fp); 
    } 

    return 0; 
} 

/* Thread associato al client */
void *client_thread(void *args)
{   

    int clientSockFd;

    struct sockaddr_in *clientSocket = (struct sockaddr_in*)args;
    int addrlenClient = sizeof(*clientSocket);

    printf("LEN %s", len);

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