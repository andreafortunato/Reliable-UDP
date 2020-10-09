// Client - FTP on UDP 
#include <arpa/inet.h> 
#include <netinet/in.h> 
#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 
#include <sys/socket.h> 
#include <sys/types.h> 
#include <unistd.h> 
  
#define IP_PROTOCOL 0 
#define IP_SERVER_ADDRESS "127.0.0.1" 
#define PORT 5555 
#define NET_BUF_SIZE 32 
  

// Main - Client
int main() 
{ 
    int sockfd; 
    int nBytes; 

    /* Struct sockaddr_in server */
    struct sockaddr_in serverSocket; 
    int addrlenServer = sizeof(serverSocket); 

    /* Struct sockaddr_in client */
    struct sockaddr_in clientSocket; 
    clientSocket.sin_family = AF_INET; 
    clientSocket.sin_port = htons(PORT); 
    clientSocket.sin_addr.s_addr = inet_addr(IP_SERVER_ADDRESS);
    int addrlenClient = sizeof(clientSocket);

    /* Buffer di rete */
    char net_buf[NET_BUF_SIZE]; 
    FILE *fp; 
  
    /* Socket - UDP */ 
    sockfd = socket(AF_INET, SOCK_DGRAM, IP_PROTOCOL); 
    if (sockfd < 0) 
        printf("\nFile descriptor not received!\n"); 
    else
        printf("\nFile descriptor %d received!\n", sockfd); 
  
    while (1) { 

        printf("\nPlease enter message for the server: "); 
        scanf("%s", net_buf); 

        sendto(sockfd, net_buf, NET_BUF_SIZE, 0, (struct sockaddr*)&clientSocket, addrlenClient); 
        
        bzero(net_buf, NET_BUF_SIZE);

        recvfrom(sockfd, net_buf, NET_BUF_SIZE, 0, (struct sockaddr*)&serverSocket, &addrlenServer); 

        printf("\n\nReceived packet from %s:%d\n", inet_ntoa(serverSocket.sin_addr), ntohs(serverSocket.sin_port));
        printf("Received: %s\n", net_buf);
        bzero(net_buf, NET_BUF_SIZE);

        printf("\n--------------------------------\n"); 
    } 

    return 0; 
} 