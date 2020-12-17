// Client - FTP on UDP 

#include <wchar.h>
#include <locale.h>
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
void *thread_consumeSegment(void *filename);


int debug = 0;              /* 1 se l'utente vuole avviare il client in modalità 'Debug' 
                               per visualizzare informazioni aggiuntive, 0 altrimenti */
int sockfd;  
int lastSeqNumSent;
int lastSeqNumRecv;
int lastAckNumSent;

Segment sndWindow[WIN_SIZE];
int sndWinFlag[WIN_SIZE];
Segment rcvWindow[WIN_SIZE];
int rcvWinFlag[WIN_SIZE];
int maxSeqNum;

pthread_mutex_t consumeLock;

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
                // fin(TRUE);

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

    memset(rcvWinFlag, 0, WIN_SIZE*sizeof(int));

    /* Comando LIST */
    tmpIntBuff = strToInt(EMPTY);
    newSegment(&sndSegment, FALSE, 1, -1, FALSE, FALSE, FALSE, "1", 1, tmpIntBuff);
    free(tmpIntBuff);
    sendto(sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket);
    lastSeqNumSent = 0;

    sndWindow[0] = *sndSegment;

    /* Inizializzazione del primo pkt atteso */
    lastAckNumSent = 1;

    wprintf(L"Lista file: \n\n");
    while(1) {

        recvSegment(sockfd, rcvSegment, &operationServerSocket, &addrlenOperationServerSocket);
        lastSeqNumRecv = atoi(rcvSegment -> seqNum);
        
        /* Calcolo slot finestra e memorizzazione segmento ricevuto e aggiornamento rcvWinFlag */
        slot = rcvWinSlot(lastSeqNumRecv, lastAckNumSent);

        //wprintf(L"\nRicevuto pacchetto - (seqNum: %d) - (slot: %d) - %d\n", atoi(rcvSegment -> seqNum), slot, rcvWinFlag[0]);

        if(!((slot < 0) || (slot > WIN_SIZE-1))) {

            rcvWindow[slot] = *rcvSegment;
            rcvWinFlag[slot] = 1;

            /* Pacchetto atteso */
            if(slot == 0) {
                
                do {
                    tmpStrBuff = intToStr(rcvWindow[slot].msg, atoi(rcvWindow[slot].lenMsg));
                    wprintf(L"%s", tmpStrBuff);
                    free(tmpStrBuff);
                    slot++;
                } while((slot != WIN_SIZE) && (rcvWinFlag[slot] != 0));
            
                lastAckNumSent = atoi(rcvWindow[slot-1].seqNum)%maxSeqNum + 1;
                
                /* Shift finestra e rcvWinFlag */
                if(slot == WIN_SIZE) {
                    memset(rcvWinFlag, 0, sizeof(int)*WIN_SIZE);
                }
                else {
                    memmove(rcvWinFlag, &rcvWinFlag[slot], sizeof(int)*(WIN_SIZE-slot));
                    memset(&rcvWinFlag[WIN_SIZE-slot], 0, sizeof(int)*slot);
                    memmove(rcvWindow, &rcvWindow[slot], sizeof(Segment)*(WIN_SIZE-slot));
                }
            }
        }
        
        /* In ogni caso invio ACK */
        tmpIntBuff = strToInt(EMPTY);
        lastSeqNumSent = lastSeqNumSent%maxSeqNum + 1;
        newSegment(&sndSegment, FALSE, lastSeqNumSent, lastAckNumSent, FALSE, TRUE, FALSE, "1", 1, tmpIntBuff);
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
    int slot;
    int totalSegsRcv = 0;
    pthread_t tid;

    // int totalSegs = WIN_SIZE;
    int totalSegs = 11; // DA AGGIORNARE CON IL NUMERO RICEVUTO DAL SERVER NELL'ULTIMO SEG DELL'HANDSHAKE

    Segment *sndSegment = NULL;
    Segment *rcvSegment = (Segment*)malloc(sizeof(Segment));
    if(rcvSegment == NULL)
    {
        wprintf(L"Error while trying to \"malloc\" a new download rcvsegment!\nClosing...\n");
        exit(-1);
    } 

    memset(rcvWinFlag, 0, WIN_SIZE*sizeof(int));

    /* Comando DOWNLOAD */
    tmpIntBuff = strToInt(filename);
    newSegment(&sndSegment, FALSE, 1, -1, FALSE, FALSE, FALSE, "2", strlen(filename), tmpIntBuff);
    wprintf(L"Filename: %s - tmpIntBuff: %s - len: %d\n", filename, intToStr(sndSegment -> msg, atoi(sndSegment -> lenMsg)), strlen(filename));
    free(tmpIntBuff);
    sendto(sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket);
    lastSeqNumSent = 0;

    sndWindow[0] = *sndSegment;

    /* Inizializzazione del primo pkt atteso */
    lastAckNumSent = 1;

    wprintf(L"Inizio ricezione file...\n");

    //
    //
    // DURANTE LA FASE DI HANDSHAKE DEL SERVER INVIARE DIMENSIONE E NOME ORIGINALE DEL FILE RICHIESTO
    //
    //
    
    /* Creazione di un thread utile alla fase di handshake */
    if(pthread_create(&tid, NULL, thread_consumeSegment, (void *)filename) != 0)
    {
        printf("New consumeSegment thread error\n");
        exit(-1);
    }

    // /* Stampa directory di esecuzione corrente */
    // char b[1024];
    // getcwd(b, 1024);
    // wprintf(L"%s", b);    

    while(1) {
        // /*********************** INIZIO RICEZIONE STUPIDA **************************************************************************/
        // recvSegment(sockfd, rcvSegment, &operationServerSocket, &addrlenOperationServerSocket);
        // lastSeqNumRecv = atoi(rcvSegment -> seqNum);
        // /* Pacchetto atteso */
        // tmpStrBuff = intToStr(rcvSegment -> msg, atoi(rcvSegment -> lenMsg));
        // wprintf(L"\nRicevuto pacchetto - (seqNum: %d) - (lenMsg: %d)\n", atoi(rcvSegment -> seqNum), atoi(rcvSegment -> lenMsg));
        // cpOnFile(wrFile, tmpStrBuff, atoi(rcvSegment -> lenMsg));
        // free(tmpStrBuff);
        // lastAckNumSent = atoi(rcvSegment -> seqNum)%maxSeqNum + 1;
        // /* In ogni caso invio ACK */
        // lastSeqNumSent = lastSeqNumSent%maxSeqNum + 1;
        // newSegment(&sndSegment, FALSE, lastSeqNumSent, lastAckNumSent, FALSE, TRUE, FALSE, "2", 1, tmpIntBuff);
        // randomSendTo(sockfd, sndSegment, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket);
        // wprintf(L"\n\nInvio ACK - (ackNum: %d)\n", atoi(sndSegment -> ackNum));
        // if(atoi(rcvSegment -> eotBit) == 1) {
        //     wprintf(L"\n\nHo ricevuto 'eotBit=1'\n");
            
        //     fclose(wrFile);
        //     fin(TRUE);

        //     break;
        // }
        // /*********************** FINE RICEZIONE STUPIDA ****************************************************************************/

        recvSegment(sockfd, rcvSegment, &operationServerSocket, &addrlenOperationServerSocket);
        lastSeqNumRecv = atoi(rcvSegment -> seqNum);
        
        /* Calcolo slot finestra e memorizzazione segmento ricevuto e aggiornamento rcvWinFlag */
        slot = normalizeDistance(lastSeqNumRecv, lastAckNumSent);

        wprintf(L"\nRicevuto pacchetto - (seqNum: %d) - (slot: %d) - %d\n", atoi(rcvSegment -> seqNum), slot, rcvWinFlag[0]);
        wprintf(L"");

        /* Se il pacchetto ricevuto è valido e non duplicato, ovvero uno dei pacchetti attesi e non un pacchetto già riscontrato e copiato su disco */
        pthread_mutex_lock(&consumeLock);
        if(slot < WIN_SIZE && rcvWinFlag[slot] == 0) {
            /* Bufferizzo il segmento appena ricevuto */
            rcvWindow[slot] = *rcvSegment;
            rcvWinFlag[slot] = 1;

            totalSegsRcv++;
        }
        pthread_mutex_unlock(&consumeLock);

        if(totalSegsRcv == totalSegs) {
            break;
        }
    }

    free(sndSegment);
    free(rcvSegment);
    free(tmpIntBuff);
}

void cpOnFile(FILE *wrFile, char *content, int len) {       
    for(int i = 0; i < len; i++) {
        fputc(content[i], wrFile);
    }
}

// DA TOGLIERE
int allReceived() {
    int allPktReceived = 1;
    for(int i=0; i<WIN_SIZE; i++) {
        if(rcvWinFlag[i] != 0) {
            allPktReceived = 0;
            break;
        }
    }
    return allPktReceived;
}

void *thread_consumeSegment(void *filename) {
    int slot;
    char *tmpStrBuff;
    FILE *wrFile = fopen((char*)filename, "wb");

    Segment *sndSegment = NULL;
    int *tmpIntBuff = strToInt(EMPTY);

    while(1) {
        slot = 0;
        /* Rimani fermo finchè il segmento atteso non è consumabile */
        while(rcvWinFlag[slot] == 0);
        pthread_mutex_lock(&consumeLock);
        /* Consuma tutti i pacchetti in ordine fino al primo non disponibile */
        do {
            tmpStrBuff = intToStr(rcvWindow[slot].msg, atoi(rcvWindow[slot].lenMsg));
            cpOnFile(wrFile, tmpStrBuff, atoi(rcvWindow[slot].lenMsg));
            wprintf(L"\n\nScritto su file il segment con seqNum: %d\n", atoi(rcvWindow[slot].seqNum));
            free(tmpStrBuff);
            fflush(wrFile);

            lastAckNumSent = lastAckNumSent%maxSeqNum + 1;
            lastSeqNumSent = lastSeqNumSent%maxSeqNum + 1;
            newSegment(&sndSegment, FALSE, lastSeqNumSent, lastAckNumSent, FALSE, TRUE, FALSE, "2", 1, tmpIntBuff);
            sendto(sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket);
            wprintf(L"Invio ACK - (ackNum: %d)\n", atoi(sndSegment -> ackNum));

            rcvWinFlag[slot] = 0;

            if(atoi(rcvWindow[slot].eotBit) == 1) {
                fclose(wrFile);

                memset(rcvWinFlag, 0, sizeof(int)*WIN_SIZE);

                free(tmpIntBuff);
                pthread_mutex_unlock(&consumeLock);
                pthread_exit(0);
            }

            slot++;
        } while(rcvWinFlag[slot] == 1 && slot < WIN_SIZE);        

        /* Slide delle strutture di ricezione */
        if(slot == WIN_SIZE) {
            memset(rcvWinFlag, 0, sizeof(int)*WIN_SIZE);
        } else {
            memmove(rcvWindow, &rcvWindow[slot], sizeof(Segment)*(WIN_SIZE-slot));
            memmove(rcvWinFlag, &rcvWinFlag[slot], sizeof(int)*(WIN_SIZE-slot));
            memset(&rcvWinFlag[WIN_SIZE-slot], 0, sizeof(int)*slot);
        }

        pthread_mutex_unlock(&consumeLock);
    }
}

void fin(char *isAck) {

    int *tmpIntBuff;

    tmpIntBuff = strToInt(EMPTY);
    /* Creazione segmenti di invio/ricezione */
    Segment *sndSegment = NULL;
    newSegment(&sndSegment, FALSE, 1, lastAckNumSent, FALSE, isAck, TRUE, EMPTY, 1, tmpIntBuff);
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

    wprintf(L"\n\n[FIN]: ACK del FIN ricevuto dal server\n");

    /* Ricezione FIN del Server */
    recvSegment(sockfd, rcvSegment, &operationServerSocket, &addrlenOperationServerSocket);

    wprintf(L"\n\n[FIN]: FIN del server ricevuto\n");

    /* Invio ACK del FIN del Server */
    lastAckNumSent = atoi(rcvSegment -> seqNum)%maxSeqNum + 1;
    tmpIntBuff = strToInt(EMPTY);
    newSegment(&sndSegment, FALSE, 2, lastAckNumSent, FALSE, TRUE, FALSE, EMPTY, 1, tmpIntBuff);
    sendto(sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket);
    wprintf(L"[FIN]: ACK del FIN inviato al server\n");
    wprintf(L"FIN terminated!\n\n");

    free(tmpIntBuff);
    free(sndSegment);
    free(rcvSegment);

    lastSeqNumRecv = 0;
}