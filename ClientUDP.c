// Client - FTP on UDP 
#include <arpa/inet.h> 
#include <netinet/in.h> 
#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 
#include <sys/socket.h> 
#include <sys/types.h> 
#include <sys/time.h>
#include <unistd.h>
#include <wchar.h>
#include <locale.h>
#include <ctype.h>
#include <errno.h>
#include "Client.h"
  
#define IP_PROTOCOL 0       /* Protocollo UDP default */

typedef struct sockaddr_in Sockaddr_in;

/* Prototipi */
void handshake();
void list();
void download(char*);
void fin();
void cpOnFile(FILE*, char*, int);
int allReceived();


int debug = 0;              /* 1 se l'utente vuole avviare il client in modalità 'Debug' 
                               per visualizzare informazioni aggiuntive, 0 altrimenti */
int sockfd;  
int lastSeqNumSend;
int lastSeqNumRecv;
int lastAckNumSend;

Segment sndWindow[WIN_SIZE];
Segment rcvWindow[WIN_SIZE];
int hashMapRcv[WIN_SIZE];
int maxSeqNum;

/* Sockaddr_in server, utilizzata per l'handshake e le richieste di operazioni */
Sockaddr_in mainServerSocket; 
int addrlenMainServerSocket;

/* Sockaddr_in server, utilizzata per l'esecuzione delle operazioni */
Sockaddr_in operationServerSocket; 
int addrlenOperationServerSocket;

// Main - Client
int main(int argc, char *argv[]) 
{ 
	setbuf(stdout,NULL);
    setlocale(LC_ALL, "");
	system("clear");

    int choice;

    char filename[256];
    bzero(filename, 256);

    char *ip = malloc(16*sizeof(char));
    if(ip == NULL)
    {
        wprintf(L"malloc on ip address failed!\n");
        exit(-1);
    }

    /* Parse dei parametri passati da riga di comando */
    // int port = parseCmdLine(argc, argv, "client", &ip, &debug); 
    // if(port == -1)
    // {
    //     exit(0);
    // }    

    maxSeqNum = WIN_SIZE*2;

    mainServerSocket.sin_family = AF_INET;
    //mainServerSocket.sin_addr.s_addr = inet_addr(ip);
    //mainServerSocket.sin_port = htons(port); 
    mainServerSocket.sin_addr.s_addr = inet_addr("127.0.0.1");
    mainServerSocket.sin_port = htons(47435); 
    addrlenMainServerSocket = sizeof(mainServerSocket);
    addrlenOperationServerSocket = sizeof(operationServerSocket);

    /* Creazione socket - UDP */ 
    sockfd = socket(AF_INET, SOCK_DGRAM, IP_PROTOCOL);
    if (sockfd < 0) {
    	wprintf(L"\nFile descriptor not received!\n");
    	exit(-1);
    }

    /* Configurazione dimensione buffer di ricezione */
    // int sockBufLen = SOCKBUFLEN;
    // if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUFFORCE, &sockBufLen, sizeof(int)) == -1) {
    //     wprintf(L"Error while setting SO_RCVBUF for socket %d: %s\n", sockfd, strerror(errno));
    //     exit(-1);
    // }
    /* Configurazione dimensione buffer di invio */
    // if (setsockopt(sockfd, SOL_SOCKET, SO_SNDBUFFORCE, &sockBufLen, sizeof(int)) == -1) {
    //     wprintf(L"Error while setting SO_SNDBUF for socket %d: %s\n", sockfd, strerror(errno));
    //     exit(-1);
    // } 

    // /* Timer associato al SYN */
    // struct timeval synTimeout;
    // synTimeout.tv_sec = 2;
    // synTimeout.tv_usec = 500*1000;
    // if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &synTimeout, sizeof(synTimeout)) < 0) {
    //     wprintf(L"Error");
    //     exit(-1);
    // }

	Segment *rcvSegment = (Segment*)malloc(sizeof(Segment));
	if(rcvSegment == NULL)
	{
		wprintf(L"Error while trying to \"malloc\" a new Segment!\nClosing...\n");
		exit(-1);
	} 
	bzero(rcvSegment, sizeof(Segment));   

    while (1) {
        //system("clear");
        choice = clientChoice();

        switch(choice) {

            case 1:
                /* Fase di handshake */
                handshake();

                /* Operazione list */
                list();

                /* Fase di chiusura */
                fin(TRUE);

                break;

            case 2:

                /* Comando download */
                wprintf(L"Filename: ");
                scanf("%s", filename);

                /* Fase di handshake */
                handshake();

                /* Operazione download */
                download(filename);

                /* Fase di chiusura */
                fin(TRUE);

                break;

            case 3:
                handshake();
                break;

            case 4:
                exit(0);
                break;
        }

    } 

    return 0; 
}

void handshake() {

    int *tmpIntBuff;

    tmpIntBuff = strToInt(EMPTY);
    /* Creazione segmenti di invio/ricezione */
    Segment *sndSegment = NULL;
    newSegment(&sndSegment, FALSE, 1, -1, TRUE, FALSE, FALSE, EMPTY, 1, tmpIntBuff);
    free(tmpIntBuff);

    Segment *rcvSegment = (Segment*)malloc(sizeof(Segment));
    if(rcvSegment == NULL)
    {
        wprintf(L"Error while trying to \"malloc\" a new handshake segment!\nClosing...\n");
        exit(-1);
    } 
    bzero(rcvSegment, sizeof(Segment));   

    /* Fase di handshake 3-way */
    do {
        /* Invio SYN */
        sendto(sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&mainServerSocket, addrlenMainServerSocket);
        wprintf(L"\n[SYN]: Sent to the server (%s:%d)\n", inet_ntoa(mainServerSocket.sin_addr), ntohs(mainServerSocket.sin_port));

        /* Ricezione SYN-ACK */
    } while(recvSegment(sockfd, rcvSegment, &operationServerSocket, &addrlenOperationServerSocket) < 0);

    /* SYN-ACK ricevuto */
    wprintf(L"[SYN-ACK]: Server information: (%s:%d)\n", inet_ntoa(operationServerSocket.sin_addr), ntohs(operationServerSocket.sin_port));
    
    /* Invio ACK del SYN-ACK */
    int ackNum = atoi(rcvSegment -> seqNum) + 1;
    tmpIntBuff = strToInt(EMPTY);
    newSegment(&sndSegment, FALSE, 2, ackNum, FALSE, TRUE, FALSE, EMPTY, 1, tmpIntBuff);
    free(tmpIntBuff);
    sendto(sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket);
    wprintf(L"[ACK]: Sent to the server (%s:%d)\n", inet_ntoa(operationServerSocket.sin_addr), ntohs(operationServerSocket.sin_port));
    wprintf(L"Handshake terminated!\n\n");
    /* Fine handshake */

}

void list() {

    int *tmpIntBuff;
    char *tmpStrBuff;

    int slot;

    Segment *sndSegment = NULL;
    Segment *rcvSegment = (Segment*)malloc(sizeof(Segment));
    if(rcvSegment == NULL)
    {
        wprintf(L"Error while trying to \"malloc\" a new list rcvsegment!\nClosing...\n");
        exit(-1);
    } 

    memset(hashMapRcv, 0, WIN_SIZE*sizeof(int));

    /* Comando LIST */
    tmpIntBuff = strToInt(EMPTY);
    newSegment(&sndSegment, FALSE, 1, -1, FALSE, FALSE, FALSE, "1", 1, tmpIntBuff);
    free(tmpIntBuff);
    sendto(sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket);
    lastSeqNumSend = 0;

    sndWindow[0] = *sndSegment;

    /* Inizializzazione del primo pkt atteso */
    lastAckNumSend = 1;

    wprintf(L"Lista file: \n\n");
    while(1) {

        recvSegment(sockfd, rcvSegment, &operationServerSocket, &addrlenOperationServerSocket);
        lastSeqNumRecv = atoi(rcvSegment -> seqNum);
        
        /* Calcolo slot finestra e memorizzazione segmento ricevuto e aggiornamento hashMapRcv */
        slot = winSlot(lastSeqNumRecv, lastAckNumSend);

        //wprintf(L"\nRicevuto pacchetto - (seqNum: %d) - (slot: %d) - %d\n", atoi(rcvSegment -> seqNum), slot, hashMapRcv[0]);

        if(!((slot < 0) || (slot > WIN_SIZE-1))) {

            rcvWindow[slot] = *rcvSegment;
            hashMapRcv[slot] = 1;

            /* Pacchetto atteso */
            if(slot == 0) {
                
                do {
                    tmpStrBuff = intToStr(rcvWindow[slot].msg, atoi(rcvWindow[slot].lenMsg));
                    wprintf(L"%s", tmpStrBuff);
                    free(tmpStrBuff);
                    slot++;
                } while((slot != WIN_SIZE) && (hashMapRcv[slot] != 0));
            
                lastAckNumSend = atoi(rcvWindow[slot-1].seqNum)%maxSeqNum + 1;
                
                /* Shift finestra e hashMapRcv */
                if(slot == WIN_SIZE) {
                    memset(hashMapRcv, 0, sizeof(int)*WIN_SIZE);
                }
                else {
                    memmove(hashMapRcv, &hashMapRcv[slot], sizeof(int)*(WIN_SIZE-slot));
                    memset(&hashMapRcv[WIN_SIZE-slot], 0, sizeof(int)*slot);
                    memmove(rcvWindow, &rcvWindow[slot], sizeof(Segment)*(WIN_SIZE-slot));
                }
            }
        }
        
        /* In ogni caso invio ACK */
        tmpIntBuff = strToInt(EMPTY);
        lastSeqNumSend = lastSeqNumSend%maxSeqNum + 1;
        newSegment(&sndSegment, FALSE, lastSeqNumSend, lastAckNumSend, FALSE, TRUE, FALSE, "1", 1, tmpIntBuff);
        sendto(sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket);
        //wprintf(L"\n\nInvio pacchetto - (ackNum: %d)\n", atoi(sndSegment -> ackNum));
        free(tmpIntBuff);

        if(atoi(rcvSegment -> eotBit) == 1 && allReceived()) {
            break;
        }
    }

    free(sndSegment);
    free(rcvSegment);
}

void download(char *filename) {

    int *tmpIntBuff;
    char *tmpStrBuff;
    int slot;

    Segment *sndSegment = NULL;
    Segment *rcvSegment = (Segment*)malloc(sizeof(Segment));
    if(rcvSegment == NULL)
    {
        wprintf(L"Error while trying to \"malloc\" a new download rcvsegment!\nClosing...\n");
        exit(-1);
    } 

    memset(hashMapRcv, 0, WIN_SIZE*sizeof(int));

    /* Comando DOWNLOAD */
    tmpIntBuff = strToInt(filename);
    newSegment(&sndSegment, FALSE, 1, -1, FALSE, FALSE, FALSE, "2", strlen(filename), tmpIntBuff);
    wprintf(L"Filename: %s - tmpIntBuff: %s - len: %d\n", filename, intToStr(sndSegment -> msg, atoi(sndSegment -> lenMsg)), strlen(filename));
    free(tmpIntBuff);
    sendto(sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket);
    lastSeqNumSend = 0;

    sndWindow[0] = *sndSegment;

    /* Inizializzazione del primo pkt atteso */
    lastAckNumSend = 1;

    wprintf(L"Inizio ricezione file...\n");
    FILE *wrFile = fopen(filename, "wb");

    char b[1024];
    getcwd(b, 1024);
    wprintf(L"%s", b);

    while(1) {

        recvSegment(sockfd, rcvSegment, &operationServerSocket, &addrlenOperationServerSocket);
        lastSeqNumRecv = atoi(rcvSegment -> seqNum);
        
        /* Calcolo slot finestra e memorizzazione segmento ricevuto e aggiornamento hashMapRcv */
        slot = winSlot(lastSeqNumRecv, lastAckNumSend);

        //wprintf(L"\nRicevuto pacchetto - (seqNum: %d) - (slot: %d) - %d\n", atoi(rcvSegment -> seqNum), slot, hashMapRcv[0]);

        if(!((slot < 0) || (slot > WIN_SIZE-1))) {

            rcvWindow[slot] = *rcvSegment;
            hashMapRcv[slot] = 1;

            /* Pacchetto atteso */
            if(slot == 0) {
                
                do {
                    tmpStrBuff = intToStr(rcvWindow[slot].msg, atoi(rcvWindow[slot].lenMsg));
                    cpOnFile(wrFile, tmpStrBuff, atoi(rcvWindow[slot].lenMsg));
                    free(tmpStrBuff);
                    slot++;
                } while((slot != WIN_SIZE) && (hashMapRcv[slot] != 0));
            
                lastAckNumSend = atoi(rcvWindow[slot-1].seqNum)%maxSeqNum + 1;
                
                /* Shift finestra e hashMapRcv */
                if(slot == WIN_SIZE) {
                    memset(hashMapRcv, 0, sizeof(int)*WIN_SIZE);
                }
                else {
                    memmove(hashMapRcv, &hashMapRcv[slot], sizeof(int)*(WIN_SIZE-slot));
                    memset(&hashMapRcv[WIN_SIZE-slot], 0, sizeof(int)*slot);
                    memmove(rcvWindow, &rcvWindow[slot], sizeof(Segment)*(WIN_SIZE-slot));
                }
            }
        }
        
        /* In ogni caso invio ACK */
        tmpIntBuff = strToInt(EMPTY);
        lastSeqNumSend = lastSeqNumSend%maxSeqNum + 1;
        newSegment(&sndSegment, FALSE, lastSeqNumSend, lastAckNumSend, FALSE, TRUE, FALSE, "1", 1, tmpIntBuff);
        sendto(sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket);
        //wprintf(L"\n\nInvio pacchetto - (ackNum: %d)\n", atoi(sndSegment -> ackNum));
        free(tmpIntBuff);

        if(atoi(rcvSegment -> eotBit) == 1 && allReceived()) {
            break;
        }
    }

    free(sndSegment);
    free(rcvSegment);
    
    fclose(wrFile);
}

void cpOnFile(FILE *wrFile, char *content, int len) {       
    for(int i = 0; i < len; i++) {
        fputc(content[i], wrFile);
    }
}

int allReceived() {
    int allPktReceived = 1;
    for(int i=0; i<WIN_SIZE; i++) {
        if(hashMapRcv[i] != 0) {
            allPktReceived = 0;
            break;
        }
    }
    return allPktReceived;
}

void fin(char *isAck) {

    int *tmpIntBuff;

    tmpIntBuff = strToInt(EMPTY);
    /* Creazione segmenti di invio/ricezione */
    Segment *sndSegment = NULL;
    newSegment(&sndSegment, FALSE, 1, lastAckNumSend, FALSE, isAck, TRUE, EMPTY, 1, tmpIntBuff);
    free(tmpIntBuff);

    Segment *rcvSegment = (Segment*)malloc(sizeof(Segment));
    if(rcvSegment == NULL)
    {
        wprintf(L"Error while trying to \"malloc\" a new handshake segment!\nClosing...\n");
        exit(-1);
    }  

    do {
        /* Invio FIN ed eventuale ACK dell'ultimo pkt ricevuto */
        sendto(sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket);
        wprintf(L"\n\n[FIN]: Sent to the server (%s:%d)\n", inet_ntoa(operationServerSocket.sin_addr), ntohs(operationServerSocket.sin_port));

        /* Ricezione ACK del FIN */
    } while(recvSegment(sockfd, rcvSegment, &operationServerSocket, &addrlenOperationServerSocket) < 0);

    /* Ricezione FIN del Server */
    recvSegment(sockfd, rcvSegment, &operationServerSocket, &addrlenOperationServerSocket);

    /* FIN del Server ricevuto */
    wprintf(L"[FIN-ACK]: Server information: (%s:%d)\n", inet_ntoa(operationServerSocket.sin_addr), ntohs(operationServerSocket.sin_port));
    
    /* Invio ACK del FIN del Server */
    lastAckNumSend = atoi(rcvSegment -> seqNum)%maxSeqNum + 1;
    tmpIntBuff = strToInt(EMPTY);
    newSegment(&sndSegment, FALSE, 2, lastAckNumSend, FALSE, TRUE, FALSE, EMPTY, 1, tmpIntBuff);
    sendto(sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket);
    wprintf(L"[ACK-FIN]: Sent to the server (%s:%d)\n", inet_ntoa(operationServerSocket.sin_addr), ntohs(operationServerSocket.sin_port));
    wprintf(L"FIN terminated!\n\n");

    free(tmpIntBuff);
    free(sndSegment);
    free(rcvSegment);

    lastSeqNumRecv = 0;
}