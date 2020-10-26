// Client - FTP on UDP 
#include <arpa/inet.h> 
#include <netinet/in.h> 
#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 
#include <sys/socket.h> 
#include <sys/types.h> 
#include <unistd.h>
#include <wchar.h>
#include <locale.h>
#include <ctype.h>
#include "Client.h"
  
#define IP_PROTOCOL 0       /* Protocollo UDP default */

typedef struct sockaddr_in Sockaddr_in;

int debug = 0;              /* 1 se l'utente vuole avviare il client in modalità 'Debug' 
                               per visualizzare informazioni aggiuntive, 0 altrimenti */

int lastSeqNumSend;
int lastSeqNumRecv;

// Main - Client
int main(int argc, char *argv[]) 
{ 
	setbuf(stdout,NULL);
    setlocale(LC_ALL, "");
	system("clear");

    int sockfd;  
    int ret;
    int choice;

    char filename[256];

    char *ip = malloc(16*sizeof(char));
    if(ip == NULL)
    {
        wprintf(L"malloc on ip address failed!\n");
        exit(-1);
    }

    /* Parse dei parametri passati da riga di comando */
    int port = parseCmdLine(argc, argv, "client", &ip, &debug); 
    if(port == -1)
    {
        exit(0);
    }    

    /* Sockaddr_in server, utilizzata per l'handshake e le richieste di operazioni */
    Sockaddr_in mainServerSocket; 
    mainServerSocket.sin_family = AF_INET;
    mainServerSocket.sin_addr.s_addr = inet_addr(ip);
    mainServerSocket.sin_port = htons(port); 
    int addrlenMainServerSocket = sizeof(mainServerSocket);

    /* Sockaddr_in server, utilizzata per l'esecuzione delle operazioni */
    Sockaddr_in operationServerSocket; 
    int addrlenOperationServerSocket = sizeof(operationServerSocket);

    /* Creazione socket - UDP */ 
    sockfd = socket(AF_INET, SOCK_DGRAM, IP_PROTOCOL);
    if (sockfd < 0) {
    	wprintf(L"\nFile descriptor not received!\n");
    	exit(-1);
    }
    
    /* Creazione segmenti di invio/ricezione */
  	Segment *sndSegment = mallocSegment("1", EMPTY, TRUE, FALSE, FALSE, EMPTY, EMPTY);

	Segment *rcvSegment = (Segment*)malloc(sizeof(Segment));
	if(rcvSegment == NULL)
	{
		wprintf(L"Error while trying to \"malloc\" a new Segment!\nClosing...\n");
		exit(-1);
	} 
	bzero(rcvSegment, sizeof(Segment));

    /* Fase di handshake 3-way */
    /* Invio SYN */
	sendto(sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&mainServerSocket, addrlenMainServerSocket);
	wprintf(L"SYN sent to the server (%s:%d)\n", inet_ntoa(mainServerSocket.sin_addr), ntohs(mainServerSocket.sin_port));

	/* Ricezione SYN-ACK */
    recvSegment(sockfd, rcvSegment, &operationServerSocket, &addrlenOperationServerSocket);
	wprintf(L"\n[PKT_RECV]: Server information: (%s:%d)\n", inet_ntoa(operationServerSocket.sin_addr), ntohs(operationServerSocket.sin_port));
	
    /* Invio ACK del SYN-ACK */
    char ackNum[MAX_SEQ_ACK_NUM];
    sprintf(ackNum, "%d", atoi(rcvSegment -> seqNum) + 1);
	newSegment(sndSegment, "2", ackNum, FALSE, TRUE, FALSE, EMPTY, EMPTY);
	sendto(sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket);
	wprintf(L"\nACK sent to the server (%s:%d)\n", inet_ntoa(operationServerSocket.sin_addr), ntohs(operationServerSocket.sin_port));
	wprintf(L"\nHandshake terminated!\n");
	/* Fine handshake */
    
    while (1) {
        //system("clear");
        choice = clientChoice();

        switch(choice) {
            case 1:
                newSegment(sndSegment, "1", EMPTY, FALSE, FALSE, FALSE, "1", EMPTY);
                sendto(sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&mainServerSocket, addrlenMainServerSocket);
                lastSeqNumSend = 1;
                
                recvSegment(sockfd, rcvSegment, &operationServerSocket, &addrlenOperationServerSocket);
                lastSeqNumRecv = atoi(rcvSegment -> seqNum);

                wprintf(L"File list:\n%s\n", rcvSegment -> msg);
                break;

            case 2:
                newSegment(sndSegment, "1", EMPTY, FALSE, FALSE, FALSE, "2", EMPTY);
                wprintf(L"Filename: ");
                scanf("%s", sndSegment -> msg);
                sendto(sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&mainServerSocket, addrlenMainServerSocket);
                
                recvSegment(sockfd, rcvSegment, &operationServerSocket, &addrlenOperationServerSocket);
                wprintf(L"File: %s", rcvSegment -> msg);

                break;

            case 3:
                break;

            case 4:
                break;
        }

    } 

    return 0; 
}