// Client - FTP on UDP 

#include <wchar.h>
#include <locale.h>
#include "Client.h"
  
#define IP_PROTOCOL 0       /* Protocollo UDP default */

typedef struct sockaddr_in Sockaddr_in;

/* Prototipi */
int handshake();
void list();
void download(char*);
void fin();
void cpOnFile(FILE*, char*, int);
int allReceived();
void *thread_consumeSegment(void *filename);

int debug = 0;              /* 1 se l'utente vuole avviare il client in modalità 'Debug' 
                               per visualizzare informazioni aggiuntive, 0 altrimenti */

/* Variabili per il download */
int sockfd;  
int lastSeqNumSent;
int lastSeqNumRecv;
int lastAckNumSent;
int lastSegmentAcked;
int totalSegs;
int totalBytes;
int canSendAck;
struct timeval tvDownloadTime;
char originalFileSHA256[65];

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
    // mainServerSocket.sin_addr.s_addr = inet_addr(ip);
    // mainServerSocket.sin_port = htons(port); 
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
        wprintf(L"\n\nPID: %d", getpid());
        choice = clientChoice();

        switch(choice) {

            case 1:
                /* Fase di handshake */
                if(handshake() == -1) {
                    wprintf(L"\nError during handshake phase\n");
                    break;  
                } 

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
                if(handshake() == -1) {
                    wprintf(L"\nError during handshake phase\n");
                    break;  
                } 

                /* Operazione download */
                download(filename);

                /* Fase di chiusura */
                fin(TRUE);

                break;

            case 3:
                /* Fase di handshake */
                if(handshake() == -1) {
                    wprintf(L"\nError during handshake phase\n");
                    break;  
                } 
                break;

            case 4:
                exit(0);
                break;
        }

    } 

    return 0; 
}

int handshake() {

    int *tmpIntBuff;
    int countRetransmission = 1;

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

    /* Timer associato al SYN */
    struct timeval synTimeout;
    synTimeout.tv_usec = 0;
    /* Fase di handshake 3-way */
    do {

        /* Tenta la ritrasmissione al massimo 10 volte incrementando di volta in volta il timeout */
        switch(countRetransmission) {
            case 1:
                synTimeout.tv_sec = 1;
                break;

            case 2:
                synTimeout.tv_sec = 3;
                break;

            case 11:
                synTimeout.tv_sec = 0;  // Reset del timeout a 0 (= attendi all'infinito)
                return -1;
                break;

            default:
                synTimeout.tv_sec = 7;
                break;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &synTimeout, sizeof(synTimeout)) < 0) {
            wprintf(L"\nError while setting syn-timeout at try n°: %d\n", countRetransmission);
            exit(-1);
        }

        sendto(sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&mainServerSocket, addrlenMainServerSocket);
        wprintf(L"\n[SYN]: Sent to the server (%s:%d)\n", inet_ntoa(mainServerSocket.sin_addr), ntohs(mainServerSocket.sin_port));
        // /* Invio SYN */
        // if(randomSendTo(sockfd, sndSegment, (struct sockaddr*)&mainServerSocket, addrlenMainServerSocket))
        //     wprintf(L"\n[SYN]: Sent to the server (%s:%d)\n", inet_ntoa(mainServerSocket.sin_addr), ntohs(mainServerSocket.sin_port));
        // else
        //     wprintf(L"\n[SYN]: LOST\n");

        countRetransmission += 1;

        /* Ricezione SYN-ACK */
    } while(recvSegment(sockfd, rcvSegment, &operationServerSocket, &addrlenOperationServerSocket) < 0 || atoi(rcvSegment -> synBit) != 1 || atoi(rcvSegment -> ackBit) != 1);

    synTimeout.tv_sec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &synTimeout, sizeof(synTimeout)) < 0) {
        wprintf(L"\nError while setting syn-timeout at try n°: %d\n", countRetransmission);
        exit(-1);
    }

    /* SYN-ACK ricevuto */
    wprintf(L"[SYN-ACK]: Server information: (%s:%d)\n", inet_ntoa(operationServerSocket.sin_addr), ntohs(operationServerSocket.sin_port));
    
    /* Invio ACK del SYN-ACK */
    int ackNum = atoi(rcvSegment -> seqNum) + 1;
    tmpIntBuff = strToInt(EMPTY);
    newSegment(&sndSegment, FALSE, 2, ackNum, FALSE, TRUE, FALSE, EMPTY, 1, tmpIntBuff);
    free(tmpIntBuff);
    
    // sendto(sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket);
    // wprintf(L"[ACK]: Sent to the server (%s:%d)\n", inet_ntoa(operationServerSocket.sin_addr), ntohs(operationServerSocket.sin_port));
    // if(randomSendTo(sockfd, sndSegment, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket))
    //     wprintf(L"[ACK]: Sent to the server (%s:%d)\n", inet_ntoa(operationServerSocket.sin_addr), ntohs(operationServerSocket.sin_port));
    // else
    //     wprintf(L"\n[ACK]: LOST\n");
    
    /* Fine handshake */
    wprintf(L"Handshake terminated!\n\n");
    return 0;
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
    newSegment(&sndSegment, FALSE, 1, -1, FALSE, TRUE, FALSE, "1", 1, tmpIntBuff);
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
    char *tmpStrBuff;
    char *tmpStr;

    int slot;
    int totalSegsRcv = 0;
    totalSegs = WIN_SIZE;
    int firstArrived = 0;
    pthread_t tid;

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
    newSegment(&sndSegment, FALSE, 1, -1, FALSE, TRUE, FALSE, "2", strlen(filename), tmpIntBuff);
    wprintf(L"Filename: %s - tmpIntBuff: %s - len: %d\n", filename, intToStr(sndSegment -> msg, atoi(sndSegment -> lenMsg)), strlen(filename));
    free(tmpIntBuff);
    sendto(sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket);
    lastSeqNumSent = 0;

    sndWindow[0] = *sndSegment;

    /* Inizializzazione del primo pkt atteso */
    lastAckNumSent = 1;

    wprintf(L"Inizio ricezione file...\n\n");

    gettimeofday(&tvDownloadTime, NULL);

    // /* Stampa directory di esecuzione corrente */
    // char b[1024];
    // getcwd(b, 1024);
    // wprintf(L"%s", b);    
    canSendAck = 0;
    while(1) {

        recvSegment(sockfd, rcvSegment, &operationServerSocket, &addrlenOperationServerSocket);
        lastSeqNumRecv = atoi(rcvSegment -> seqNum);

        //wprintf(L"\nRicevuto pacchetto - (seqNum: %d)\n", atoi(rcvSegment -> seqNum));

        /* Se il pacchetto ricevuto è valido e non duplicato, ovvero uno dei pacchetti attesi e non un pacchetto già riscontrato e copiato su disco */
        pthread_mutex_lock(&consumeLock);

        /* Calcolo slot finestra e memorizzazione segmento ricevuto e aggiornamento rcvWinFlag */
        slot = normalizeDistance(lastSeqNumRecv, lastAckNumSent);

        /* Primo pacchetto ricevuto dal server contenente filename e dimensione */
        if(firstArrived == 0 && slot == 0) {
            firstArrived = 1;

            tmpStrBuff = intToStr(rcvSegment -> msg, atoi(rcvSegment -> lenMsg));
            //wprintf(L"\nPACCHETTO INIZIALE RICEVUTO: %s, LEN: %d\n", tmpStrBuff, atoi(rcvSegment -> lenMsg));

            tmpStr = strtok(tmpStrBuff, "\b");
            totalSegs = atoi(tmpStr);
            //wprintf(L"\nASPETTO %d SEGMENTI...\n", totalSegs);

            tmpStr = strtok(NULL, "\b");
            strcpy(filename, tmpStr);
            //wprintf(L"\nNOME ORIGINALE: %s...\n", filename);

            tmpStr = strtok(NULL, "\b");
            totalBytes = atoi(tmpStr);
            //wprintf(L"\nASPETTO %d BYTES...\n", totalBytes);

            tmpStr = strtok(NULL, "\b");
            strcpy(originalFileSHA256, tmpStr);
            // wprintf(L"\nSHA256: %s\n", originalFileSHA256);

            free(tmpStrBuff);

            rcvWindow[0] = *rcvSegment;
            rcvWinFlag[0] = 1;

            totalSegsRcv++;

            /* Creazione di un thread per la scrittura dei segmenti su disco */
            if(pthread_create(&tid, NULL, thread_consumeSegment, (void *)filename) != 0)
            {
                wprintf(L"New consumeSegment thread error\n");
                exit(-1);
            }
        } 
        else {
            if(slot < WIN_SIZE && rcvWinFlag[slot] == 0) {
                /* Bufferizzo il segmento appena ricevuto */
                rcvWindow[slot] = *rcvSegment;
                rcvWinFlag[slot] = 1;
                //wprintf(L"Caricato pacchetto nello slot: %d\n", slot);
                totalSegsRcv++;
            }
        }

        pthread_mutex_unlock(&consumeLock);

        // Se è consumabile aspetto che il consume scriva su file
        if(rcvWinFlag[0] == 1) {
            //wprintf(L"\nE' consumabile: %d", atoi(rcvWindow[0].seqNum));
            while(canSendAck == 0); // Attendi che il consume abbia consumato e aggiornato lastAckNumSent
            canSendAck = 0;
        }

        lastSeqNumSent = lastSeqNumSent%maxSeqNum + 1;

        tmpIntBuff = strToInt(rcvSegment -> seqNum);
        newSegment(&sndSegment, FALSE, lastSeqNumSent, lastAckNumSent, FALSE, TRUE, FALSE, "2", strlen(rcvSegment -> seqNum), tmpIntBuff);
        sendto(sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket);
        //wprintf(L"\nInvio ACK (seqNum: %d) - (ackNum: %d) - (seqNumAcked: %d)\n", atoi(sndSegment -> seqNum), atoi(sndSegment -> ackNum), atoi(rcvSegment -> seqNum));
        free(tmpIntBuff);

        if(totalSegsRcv == totalSegs) {
            pthread_join(tid, NULL);
            //wprintf(L"\nThread consume joinato...\n");
            break;
        }
    }

    free(sndSegment);
    free(rcvSegment);
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
    int firstArrived = 0;
    int consumedSegments = 0;
    int bytesRecv = 0;
    char *tmpStrBuff;
    char *currentFileSHA256;
    
    FILE *wrFile = fopen((char*)filename, "wb");

    while(1) {

        /* Rimani fermo finchè il segmento atteso non è consumabile */
        while(rcvWinFlag[0] == 0);
        pthread_mutex_lock(&consumeLock);

        if(firstArrived == 0) {

            firstArrived = 1;

            lastAckNumSent = 2; // Riscontro solo il primo pacchetto contentendo info su dimensione file

            memmove(rcvWindow, &rcvWindow[1], sizeof(Segment)*(WIN_SIZE-1));
            memmove(rcvWinFlag, &rcvWinFlag[1], sizeof(int)*(WIN_SIZE-1));
            memset(&rcvWinFlag[WIN_SIZE-1], 0, sizeof(int)*1);

            /* Sblocco recv_thread per invio ack */
            canSendAck = 1; 

            pthread_mutex_unlock(&consumeLock);
            continue;
        }

        slot = 0;
        /* Consuma tutti i pacchetti in ordine fino al primo non disponibile */
        do {
            tmpStrBuff = intToStr(rcvWindow[slot].msg, atoi(rcvWindow[slot].lenMsg));
            cpOnFile(wrFile, tmpStrBuff, atoi(rcvWindow[slot].lenMsg));
            //wprintf(L"BYTES RECEIVED: %d\n", atoi(rcvWindow[slot].lenMsg));
            bytesRecv = bytesRecv + atoi(rcvWindow[slot].lenMsg);
            //wprintf(L"\nScritto su file il segment con seqNum: %d\n", atoi(rcvWindow[slot].seqNum));
            free(tmpStrBuff);
            fflush(wrFile);

            rcvWinFlag[slot] = 0;

            /* Percentuale di segmenti ricevuti */
            consumedSegments++;
            wprintf(L"\rDownload: %.2f%% (%d/%d bytes received)", (consumedSegments*100)/(float)(totalSegs-1), bytesRecv, totalBytes);

            if(atoi(rcvWindow[slot].eotBit) == 1) {

                /* AckNum da inviare al server */
                lastAckNumSent = atoi(rcvWindow[slot].seqNum)%maxSeqNum + 1;

                lastSegmentAcked = atoi(rcvWindow[slot].seqNum);

                /* Sblocco recv_thread per invio ack */
                canSendAck = 1;

                fclose(wrFile);

                memset(rcvWinFlag, 0, sizeof(int)*WIN_SIZE);

                pthread_mutex_unlock(&consumeLock);

                wprintf(L"\nDownload time: %.2f sec", elapsedTime(tvDownloadTime)/1000);

                wprintf(L"\nCalculating SHA256...");

                currentFileSHA256 = getFileSHA256(filename);

                if(strcmp(currentFileSHA256, originalFileSHA256) == 0)
                    wprintf(L"\rDownload completed successfully!\nSHA256: %s\n", currentFileSHA256);
                else
                    wprintf(L"\rThere was an error, download gone wrong!\nOriginal SHA256: %s\n Current SHA256: %s\n", originalFileSHA256, currentFileSHA256);

                pthread_exit(0);
            }

            slot++;
        } while(rcvWinFlag[slot] == 1 && slot < WIN_SIZE);        

        /* AckNum da inviare al server */
        lastAckNumSent = atoi(rcvWindow[slot-1].seqNum)%maxSeqNum + 1;
        //wprintf(L"\n[CONSUME] -> ackNum da inviare: %d", lastAckNumSent);

        /* Slide delle strutture di ricezione */
        if(slot == WIN_SIZE) {
            memset(rcvWinFlag, 0, sizeof(int)*WIN_SIZE);
        } else {
            memmove(rcvWindow, &rcvWindow[slot], sizeof(Segment)*(WIN_SIZE-slot));
            memmove(rcvWinFlag, &rcvWinFlag[slot], sizeof(int)*(WIN_SIZE-slot));
            memset(&rcvWinFlag[WIN_SIZE-slot], 0, sizeof(int)*slot);
        }

        /* Sblocco recv_thread per invio ack */
        canSendAck = 1; 

        pthread_mutex_unlock(&consumeLock);
    }
}

void fin(char *isAck) {

    int *tmpIntBuff;

    /* Creazione segmenti di invio/ricezione */
    char tmpStrBuff[WIN];
    sprintf(tmpStrBuff, "%d", lastSegmentAcked);
    tmpIntBuff = strToInt(tmpStrBuff);
    
    Segment *sndSegment = NULL;
    newSegment(&sndSegment, FALSE, 1, lastAckNumSent, FALSE, isAck, TRUE, EMPTY, strlen(tmpStrBuff), tmpIntBuff);
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
        //wprintf(L"\n\n[FIN]: Sent to the server (%s:%d)\n", inet_ntoa(operationServerSocket.sin_addr), ntohs(operationServerSocket.sin_port));

        /* Ricezione ACK del FIN */
    } while(recvSegment(sockfd, rcvSegment, &operationServerSocket, &addrlenOperationServerSocket) < 0);

    //wprintf(L"\n\n[FIN]: ACK del FIN ricevuto dal server\n");

    /* Ricezione FIN del Server */
    recvSegment(sockfd, rcvSegment, &operationServerSocket, &addrlenOperationServerSocket);

    //wprintf(L"\n\n[FIN]: FIN del server ricevuto\n");

    /* Invio ACK del FIN del Server */
    lastAckNumSent = atoi(rcvSegment -> seqNum)%maxSeqNum + 1;
    tmpIntBuff = strToInt(EMPTY);
    newSegment(&sndSegment, FALSE, 2, lastAckNumSent, FALSE, TRUE, FALSE, EMPTY, 1, tmpIntBuff);
    sendto(sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket);
    //wprintf(L"[FIN]: ACK del FIN inviato al server\n");
    //wprintf(L"FIN terminated!\n\n");

    free(tmpIntBuff);
    free(sndSegment);
    free(rcvSegment);

    lastSeqNumRecv = 0;
}