// Client - FTP on UDP 
#include <arpa/inet.h> 
#include <netinet/in.h> 
#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 
#include <sys/socket.h> 
#include <sys/types.h> 
#include <unistd.h> 
#include "Common.h"
  
#define IP_PROTOCOL 0 
#define IP_SERVER_ADDRESS "127.0.0.1" 
#define PORT 5555 
#define NET_BUF_SIZE 32 

// Main - Client
int main() 
{ 
	setbuf(stdout,NULL);
	system("clear");

    int sockfd;  
    int ret = 0;

    /* Struct sockaddr_in client */
    struct sockaddr_in mainServerSocket; 
    mainServerSocket.sin_family = AF_INET; 
    mainServerSocket.sin_port = htons(PORT); 
    mainServerSocket.sin_addr.s_addr = inet_addr(IP_SERVER_ADDRESS);
    int addrlenMainServerSocket = sizeof(mainServerSocket);

    /* Struct sockaddr_in client */
    struct sockaddr_in operationServerSocket; 
    int addrlenOperationServerSocket = sizeof(operationServerSocket);

    /* Socket - UDP */ 
    sockfd = socket(AF_INET, SOCK_DGRAM, IP_PROTOCOL); 
    if (sockfd < 0) {
    	printf("\nFile descriptor not received!\n"); 
    	exit(-1);
    }
    
    printf("\nFile descriptor %d received!\n", sockfd); 
  	
  	Segment *sndSegment = mallocSegment("0", EMPTY, TRUE, FALSE, FALSE, "1", "SYN");

	Segment *rcvSegment = (Segment*)malloc(sizeof(Segment));
	if(rcvSegment == NULL)
	{
		printf("Error while trying to \"malloc\" a new Segment!\nClosing...\n");
		exit(-1);
	} 
	bzero(rcvSegment, sizeof(Segment));

    /* Fase di handshake 3-way */
    /* Invio SYN */
	sendto(sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&mainServerSocket, addrlenMainServerSocket);
	printf("SYN sent to the server (%s:%d)\n", inet_ntoa(mainServerSocket.sin_addr), ntohs(mainServerSocket.sin_port));

	/* Ricezione SYN-ACK */
	recvfrom(sockfd, rcvSegment, sizeof(Segment), 0, (struct sockaddr*)&operationServerSocket, (socklen_t*)&addrlenOperationServerSocket);
	printf("\n[PKT_RECV]: Return value %d, Server information: (%s:%d)\n", ret, inet_ntoa(operationServerSocket.sin_addr), ntohs(operationServerSocket.sin_port));
	printf("[MSG] %s\n", rcvSegment -> msg);
	/* Invio ACK del SYN-ACK */
	newSegment(sndSegment, "1", "1", FALSE, TRUE, FALSE, "1", "ACK del SYN-ACK");
	sendto(sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket);
	printf("\nACK sent to the server (%s:%d)\n", inet_ntoa(operationServerSocket.sin_addr), ntohs(operationServerSocket.sin_port));
	printf("\nHandshake terminated!\n");
	/* Fine handshake */
    
    close(sockfd);
    exit(0);

    while (1) { 

        // printf("\nPlease enter message for the server: "); 
        // scanf("%s", net_buf); 

        // sendto(sockfd, net_buf, NET_BUF_SIZE, 0, (struct sockaddr*)&clientSocket, addrlenClient); 
        
        // bzero(net_buf, NET_BUF_SIZE);

        // recvfrom(sockfd, net_buf, NET_BUF_SIZE, 0, (struct sockaddr*)&serverSocket, &addrlenServer); 

        // printf("\n\nReceived packet from %s:%d\n", inet_ntoa(serverSocket.sin_addr), ntohs(serverSocket.sin_port));
        // printf("Received: %s\n", net_buf);
        // bzero(net_buf, NET_BUF_SIZE);

        // printf("\n--------------------------------\n"); 
    } 

    return 0; 
} 