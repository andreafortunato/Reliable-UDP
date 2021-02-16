// Client - FTP on UDP 

#include <wchar.h>
#include <locale.h>
#include "Client.h"
  
#define IP_PROTOCOL 0       /* Protocollo UDP default */

typedef struct sockaddr_in Sockaddr_in;

/* Prototipi */
int handshake();
int list();
int download(char*);
void cpOnFile(FILE*, char*, int);
int allReceived();
void *thread_consumeSegment(void *);
void *thread_consumeFileList(void *);
void *fin_thread(void*);

void ctrl_c_handler();
void resetData();

int debug = 0;              /* 1 se l'utente vuole avviare il client in modalità 'Debug' 
                               per visualizzare informazioni aggiuntive, 0 altrimenti */
/* Variabili per il download */
int sockfd;
int blockMain;
pthread_t finTid;
int renewBind = 0;
int loss_prob = LOSS_PROB;

FILE *wrFile;

int lastSeqNumSent;
int lastSeqNumRecv;
int lastAckNumSent;
int lastSegmentAcked;
int totalSegs;
int totalBytes;
int canSendAck;
struct timeval tvDownloadTime;
char originalFileSHA256[65];
char isAck[BIT];

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
    srand(time(0));
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

    if(pthread_mutex_init(&consumeLock, NULL) != 0) {
        wprintf(L"Failed consumeLock semaphore initialization.\n");
        exit(-1);
    }

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
        wprintf(L"\nSocket file descriptor not received!\n");
        exit(-1);
    }


    Segment *rcvSegment = (Segment*)malloc(sizeof(Segment));
    if(rcvSegment == NULL)
    {
        wprintf(L"Error while trying to \"malloc\" a new Segment!\nClosing...\n");
        exit(-1);
    }

    /* Eventuale 'pulizia' del buffer, in caso siano rimasti segmenti pendenti in rete
       precedentemente inviati sulla stessa porta sulla quale è appena stata aperta la socket */
    struct timeval clearInBuffer;
    clearInBuffer.tv_sec = 0;

    while (1) {
        //system("clear");
        wprintf(L"\n\nPID: %d", getpid());
        choice = clientChoice();

        resetData();

        clearInBuffer.tv_usec = 50*1000;
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &clearInBuffer, sizeof(clearInBuffer)) < 0) {
            wprintf(L"\nError while setting clearInBuffer\n");
            exit(-1);
        }
        while(recvSegment(sockfd, rcvSegment, &operationServerSocket, &addrlenOperationServerSocket) >= 0);
        clearInBuffer.tv_usec = 0;
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &clearInBuffer, sizeof(clearInBuffer)) < 0) {
            wprintf(L"\nError while setting clearInBuffer\n");
            exit(-1);
        }

        switch(choice) {

            case 1:
                /* Fase di handshake */
                if(handshake() == -1) {
                    wprintf(L"\nError during handshake phase\n");
                    //blockMain = 0;
                    break;  
                } 

                /* Operazione list */
                if(list() == -1) {
                    //blockMain = 0;
                    break;
                }

                /* Fase di chiusura */
                strcpy(isAck, TRUE);
                if(pthread_create(&finTid, NULL, fin_thread, NULL) != 0)
                {
                    wprintf(L"New client thread error\n");
                    exit(-1);
                }
                pthread_join(finTid, NULL);

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
                if(download(filename) == -1) {
                    //blockMain = 0;
                    break;
                }

                /* Fase di chiusura */
                strcpy(isAck, TRUE);
                if(pthread_create(&finTid, NULL, fin_thread, NULL) != 0)
                {
                    wprintf(L"New client thread error\n");
                    exit(-1);
                }
                pthread_join(finTid, NULL);

                break;

            case 3:
                /* Fase di handshake */
                if(handshake() == -1) {
                    wprintf(L"\nError during handshake phase\n");
                    break;  
                } 
                break;

            case 4:
                if(pthread_mutex_destroy(&consumeLock) != 0) {
                    wprintf(L"Failed consumeLock semaphore destruction.\n");
                    exit(-1);
                }

                // EFFETTUARE FREE(strutture) e CLOSE(socket) PIU' EVENTUALI ALTRE OPERAZIONI FINALI

                exit(0);
                break;
        }
        //close(sockfd);
        while(blockMain == 1);
    } 

    return 0; 
}

void ctrl_c_handler() {
    signal(SIGINT, SIG_DFL);
    pthread_cancel(finTid);

    /* Creazione socket - UDP */
    // close(sockfd);
    // sockfd = socket(AF_INET, SOCK_DGRAM, IP_PROTOCOL);
    // if (sockfd < 0) {
    //     wprintf(L"\nSocket file descriptor not received!\n");
    //     exit(-1);
    // }
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

                free(rcvSegment);
                free(sndSegment);

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

        // sendto(sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&mainServerSocket, addrlenMainServerSocket);
        // wprintf(L"\n[SYN]: Sent to the server (%s:%d)\n", inet_ntoa(mainServerSocket.sin_addr), ntohs(mainServerSocket.sin_port));
        /* Invio SYN */
        if(randomSendTo(sockfd, sndSegment, (struct sockaddr*)&mainServerSocket, addrlenMainServerSocket, loss_prob)){
            // wprintf(L"\n[SYN]: Sent to the server (%s:%d)\n", inet_ntoa(mainServerSocket.sin_addr), ntohs(mainServerSocket.sin_port));
        }
        else{
            // wprintf(L"\n[SYN]: LOST\n");
        }

        countRetransmission += 1;

        /* Ricezione SYN-ACK */
    } while(recvSegment(sockfd, rcvSegment, &operationServerSocket, &addrlenOperationServerSocket) < 0 || atoi(rcvSegment -> synBit) != 1 || atoi(rcvSegment -> ackBit) != 1);

    synTimeout.tv_sec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &synTimeout, sizeof(synTimeout)) < 0) {
        wprintf(L"\nError while setting syn-timeout at try n°: %d\n", countRetransmission);
        exit(-1);
    }

    /* SYN-ACK ricevuto */
    //wprintf(L"[SYN-ACK]: Server information: (%s:%d)\n", inet_ntoa(operationServerSocket.sin_addr), ntohs(operationServerSocket.sin_port));
    
    /* Invio ACK del SYN-ACK */
    int ackNum = atoi(rcvSegment -> seqNum) + 1;
    tmpIntBuff = strToInt(EMPTY);
    newSegment(&sndSegment, FALSE, 2, ackNum, FALSE, TRUE, FALSE, EMPTY, 1, tmpIntBuff);
    free(tmpIntBuff);
    
    // sendto(sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket);
    // wprintf(L"[ACK]: Sent to the server (%s:%d)\n", inet_ntoa(operationServerSocket.sin_addr), ntohs(operationServerSocket.sin_port));
    if(randomSendTo(sockfd, sndSegment, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket, loss_prob)){
        // wprintf(L"[ACK]: Sent to the server (%s:%d)\n", inet_ntoa(operationServerSocket.sin_addr), ntohs(operationServerSocket.sin_port));
    }
    else{
        // wprintf(L"\n[ACK]: LOST\n");
    }
    
    free(rcvSegment);
    free(sndSegment);

    /* Fine handshake */
    wprintf(L"Handshake terminated!\n\n");
    return 0;
}

int list() {
    int *tmpIntBuff;
    char *tmpStrBuff;
    char *tmpStr;
    int countRetransmission = 1;

    int slot;
    int totalSegsRcv = 0;
    totalSegs = WIN_SIZE;
    int firstArrived = 0;
    pthread_t consumeTid = -1;

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
    newSegment(&sndSegment, FALSE, 1, 2, FALSE, TRUE, FALSE, "1", 1, tmpIntBuff);
    free(tmpIntBuff);

    /* Timer associato al File List Request */
    struct timeval requestTimeout;
    requestTimeout.tv_usec = 0;
    do {
        /* Tenta la ritrasmissione al massimo 7 volte incrementando di volta in volta il timeout (tempo totale massimo 29 secondi) */
        switch(countRetransmission) {
            case 1:
                requestTimeout.tv_sec = 1;
                break;

            case 2:
                requestTimeout.tv_sec = 3;
                break;

            case 8:
                requestTimeout.tv_sec = 0;  // Reset del timeout a 0 (= attendi all'infinito)
                if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &requestTimeout, sizeof(requestTimeout)) < 0) {
                    wprintf(L"\nError while setting fin-timeout at try n°: %d\n", countRetransmission);
                    exit(-1);
                }

                free(rcvSegment);
                free(sndSegment);
                
                return -1;
                break;

            default:
                requestTimeout.tv_sec = 5;
                break;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &requestTimeout, sizeof(requestTimeout)) < 0) {
            wprintf(L"\nError while setting fin-timeout at try n°: %d\n", countRetransmission);
            exit(-1);
        }

        wprintf(L"\nFile list request n°%d...\n", countRetransmission);
        // sendto(sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket);
        // wprintf(L"\n[LIST]: Request sent to the server (%s:%d)\n", inet_ntoa(operationServerSocket.sin_addr), ntohs(operationServerSocket.sin_port));
        /* Invio SYN */
        if(randomSendTo(sockfd, sndSegment, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket, loss_prob))
            wprintf(L"\n[LIST]: Request sent to the server (%s:%d)\n", inet_ntoa(operationServerSocket.sin_addr), ntohs(operationServerSocket.sin_port));
        else
            wprintf(L"\n[LIST]: LOST\n");

        countRetransmission += 1;
    } while(recvSegment(sockfd, rcvSegment, &operationServerSocket, &addrlenOperationServerSocket) < 0 || atoi(rcvSegment -> synBit) == 1);
    requestTimeout.tv_sec = 64;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &requestTimeout, sizeof(requestTimeout)) < 0) {
        wprintf(L"\nError while setting fin-timeout at try n°: %d\n", countRetransmission);
        exit(-1);
    }

    while(1) {

        lastSeqNumRecv = atoi(rcvSegment -> seqNum);

        //wprintf(L"%d\n", lastSeqNumRecv);

        //wprintf(L"\nRicevuto pacchetto - (seqNum: %d)\n", atoi(rcvSegment -> seqNum));

        pthread_mutex_lock(&consumeLock);

        /* Calcolo slot finestra e memorizzazione segmento ricevuto e aggiornamento rcvWinFlag */
        slot = normalizeDistance(lastSeqNumRecv, lastAckNumSent);

        //wprintf(L"lastSeqNumRecv: %d, lastAckNumSent: %d, slot: %d\n", lastSeqNumRecv, lastAckNumSent, slot);

        /* Primo pacchetto ricevuto dal server contenente filename e dimensione */
        if(firstArrived == 0 && slot == 0) {
            firstArrived = 1;

            tmpStrBuff = intToStr(rcvSegment -> msg, atoi(rcvSegment -> lenMsg));
            //wprintf(L"\nPACCHETTO INIZIALE RICEVUTO: %s, LEN: %d\n", tmpStrBuff, atoi(rcvSegment -> lenMsg));

            tmpStr = strtok(tmpStrBuff, "\b");
            totalSegs = atoi(tmpStr);
            wprintf(L"\nASPETTO %d SEGMENTI...\n", totalSegs);

            free(tmpStrBuff);

            rcvWindow[0] = *rcvSegment;
            rcvWinFlag[0] = 1;

            totalSegsRcv++;

            /* Creazione di un thread per la scrittura dei segmenti su disco */
            if(pthread_create(&consumeTid, NULL, thread_consumeFileList, NULL) != 0)
            {
                wprintf(L"New consumeSegment thread error\n");
                exit(-1);
            }
        } 
        else {
            /* Se il pacchetto ricevuto è valido e non duplicato, ovvero uno dei pacchetti attesi e non un pacchetto già riscontrato e copiato su disco */
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
        newSegment(&sndSegment, FALSE, lastSeqNumSent, lastAckNumSent, FALSE, TRUE, FALSE, "1", strlen(rcvSegment -> seqNum), tmpIntBuff);
        // sendto(sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket);
        randomSendTo(sockfd, sndSegment, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket, loss_prob);
        //wprintf(L"\nInvio ACK (seqNum: %d) - (ackNum: %d) - (seqNumAcked: %d)\n", atoi(sndSegment -> seqNum), atoi(sndSegment -> ackNum), atoi(rcvSegment -> seqNum));
        free(tmpIntBuff);

        if(totalSegsRcv == totalSegs) {
            pthread_join(consumeTid, NULL);
            //wprintf(L"\nThread consume joinato...\n");
            break;
        }

        //wprintf(L"\nAttesa nuovo pkt...\n");
        if(recvSegment(sockfd, rcvSegment, &operationServerSocket, &addrlenOperationServerSocket) < 0) {
            wprintf(L"\n[LIST] -> Server dead. List FAILED!\n");

            if(consumeTid != -1) {
                pthread_cancel(consumeTid);
                pthread_join(consumeTid, NULL);
            }

            free(sndSegment);
            free(rcvSegment);

            return -1;
        }
        //wprintf(L"[LIST] -> New pkt -> seqNum: %s, msg: %s\n", rcvSegment->seqNum, intToStr(rcvSegment->msg, atoi(rcvSegment->lenMsg)));
    }

    free(sndSegment);
    free(rcvSegment);

    return 0;
}

void *thread_consumeFileList(void *args) {
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    int slot;
    int consumedSegments = 0;
    char *tmpStrBuff;
    int move = 999;
    char *wait = "Waiting for more files...";
    char partialFileList[LEN_MSG*WIN_SIZE];
    int i;
    int lastFileLen;

    pthread_mutex_lock(&consumeLock);

    lastAckNumSent = 2; // Riscontro solo il primo pacchetto contente info su dimensione file

    memmove(rcvWindow, &rcvWindow[1], sizeof(Segment)*(WIN_SIZE-1));
    memmove(rcvWinFlag, &rcvWinFlag[1], sizeof(int)*(WIN_SIZE-1));
    memset(&rcvWinFlag[WIN_SIZE-1], 0, sizeof(int)*1);

    /* Sblocco recv_thread per invio ack */
    canSendAck = 1; 

    pthread_mutex_unlock(&consumeLock);

    wprintf(L"[CONSUME] -> Lista file: \n\n");
    wprintf(L"%s", wait);

    while(1) {

        /* Rimani fermo finchè il segmento atteso non è consumabile */
        while(rcvWinFlag[0] == 0);
        pthread_mutex_lock(&consumeLock);

        slot = 0;
        bzero(partialFileList, strlen(partialFileList));
        /* Consuma tutti i pacchetti in ordine fino al primo non disponibile */
        do {
            tmpStrBuff = intToStr(rcvWindow[slot].msg, atoi(rcvWindow[slot].lenMsg));

            strcat(partialFileList, tmpStrBuff);

            free(tmpStrBuff);

            rcvWinFlag[slot] = 0;

            /* Percentuale di segmenti ricevuti */
            consumedSegments++;

            if(atoi(rcvWindow[slot].eotBit) == 1) {

                if(move < 0)
                    wprintf(L"\033[2K\033[1A\033[%dC%s", -move, partialFileList);
                else if (move == 0)
                    wprintf(L"\033[2K\033[1A%s", partialFileList);
                else if (move > 0 && move != 999)
                    wprintf(L"\033[2K\033[1A\033[%dD%s", move, partialFileList);
                else
                    wprintf(L"\r\033[2K%s", partialFileList);

                /* AckNum da inviare al server */
                lastAckNumSent = atoi(rcvWindow[slot].seqNum)%maxSeqNum + 1;

                lastSegmentAcked = atoi(rcvWindow[slot].seqNum);

                /* Sblocco recv_thread per invio ack */
                canSendAck = 1;

                memset(rcvWinFlag, 0, sizeof(int)*WIN_SIZE);

                pthread_mutex_unlock(&consumeLock);

                pthread_exit(0);
            }

            slot++;
        } while(rcvWinFlag[slot] == 1 && slot < WIN_SIZE);

        if(move < 0)
            wprintf(L"\033[2K\033[1A\033[%dC%s", -move, partialFileList);
        else if (move == 0)
            wprintf(L"\033[2K\033[1A%s", partialFileList);
        else if (move > 0 && move != 999)
            wprintf(L"\033[2K\033[1A\033[%dD%s", move, partialFileList);
        else
            wprintf(L"\r\033[2K%s", partialFileList);

        if(partialFileList[strlen(partialFileList)-1] == '\n') {
            wprintf(L"%s", wait);
            
            move = 999;
        } else {
            wprintf(L"\n%s", wait);
            
            for(i = strlen(partialFileList)-1; i >= 0; i--) {
                if(partialFileList[i] == '\n') {
                    lastFileLen = strlen(partialFileList+i+1);
                    break;
                }
            }
            
            move = strlen(wait)-lastFileLen;
        }

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

int download(char *filename) {

    int *tmpIntBuff;
    char *tmpStrBuff;
    char *tmpStr;
    int countRetransmission = 1;

    int slot;
    int totalSegsRcv = 0;
    int firstArrived = 0;
    pthread_t consumeTid = -1;

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
    newSegment(&sndSegment, FALSE, 1, 2, FALSE, TRUE, FALSE, "2", strlen(filename), tmpIntBuff);
    free(tmpIntBuff);

    /* Timer associato al Download Request */
    struct timeval requestTimeout;
    requestTimeout.tv_usec = 0;
    do {
        /* Tenta la ritrasmissione al massimo 7 volte incrementando di volta in volta il timeout (tempo totale massimo 29 secondi) */
        switch(countRetransmission) {
            case 1:
                requestTimeout.tv_sec = 1;
                break;

            case 2:
                requestTimeout.tv_sec = 3;
                break;

            case 8:
                requestTimeout.tv_sec = 0;  // Reset del timeout a 0 (= attendi all'infinito)

                if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &requestTimeout, sizeof(requestTimeout)) < 0) {
                    wprintf(L"\nError while setting request-timeout at try n°: %d\n", countRetransmission);
                    exit(-1);
                }

                free(rcvSegment);
                free(sndSegment);
                
                return -1;
                break;

            default:
                requestTimeout.tv_sec = 5;
                break;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &requestTimeout, sizeof(requestTimeout)) < 0) {
            wprintf(L"\nError while setting request-timeout at try n°: %d\n", countRetransmission);
            exit(-1);
        }

        // wprintf(L"\nDownload request n°%d...\n", countRetransmission);
        // sendto(sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket);
        // wprintf(L"\n[DOWNLOAD]: Request sent to the server (%s:%d)\n", inet_ntoa(operationServerSocket.sin_addr), ntohs(operationServerSocket.sin_port));
        /* Invio Download Request */
        if(randomSendTo(sockfd, sndSegment, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket, loss_prob)){
            // wprintf(L"\n[DOWNLOAD]: Sent to the server (%s:%d)\n", inet_ntoa(operationServerSocket.sin_addr), ntohs(operationServerSocket.sin_port));
        }
        else{
            // wprintf(L"\n[DOWNLOAD]: LOST\n");
        }

        countRetransmission += 1;
    } while(recvSegment(sockfd, rcvSegment, &operationServerSocket, &addrlenOperationServerSocket) < 0 || atoi(rcvSegment -> synBit) == 1 || atoi(rcvSegment -> cmdType) != 2);

    /* Il file non esiste e ricevo FileNotFound */
    if(atoi(rcvSegment -> eotBit) == 1 && atoi(rcvSegment -> seqNum) == 1) {
        requestTimeout.tv_sec = 0;
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &requestTimeout, sizeof(requestTimeout)) < 0) {
            wprintf(L"\nError while setting request-timeout at try n°: %d\n", countRetransmission);
            exit(-1);
        }

        tmpIntBuff = strToInt(EMPTY);
        newSegment(&sndSegment, FALSE, 1, 2, FALSE, TRUE, FALSE, "2", 1, tmpIntBuff);
        free(tmpIntBuff);

        // sendto(sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket);
        randomSendTo(sockfd, sndSegment, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket, loss_prob);

        free(sndSegment);
        free(rcvSegment);

        wprintf(L"[\033[1;91mERROR\033[0m] File not found, please try again!\n");

        return 0;
    }
    
    /* Timeout per la ricezione di un segmento: se rimango 64 secondi in attesa, presumibilmente il
       server si è disconnesso */
    requestTimeout.tv_sec = 4; // DA CAMBIARE A 64
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &requestTimeout, sizeof(requestTimeout)) < 0) {
        wprintf(L"\nError while setting request-timeout at try n°: %d\n", countRetransmission);
        exit(-1);
    }

    wprintf(L"\nInizio ricezione file...\n");
    wprintf(L"\nDownload Speed\t Average Speed\t Bytes\n");
    wprintf(L"0.00kB/s\t 0.00kB/s\t 0\n\n");

    wprintf(L" %lc", 0x258e);
    for (int i = 0; i < 50; i++) {
        wprintf(L"%lc", 0x2591);
    }
    wprintf(L"%lc\n\n\n\033[3A", 0x2595);

    gettimeofday(&tvDownloadTime, NULL);

    // /* Stampa directory di esecuzione corrente */
    // char b[1024];
    // getcwd(b, 1024);
    // wprintf(L"%s", b);    
    while(1) {
        
        lastSeqNumRecv = atoi(rcvSegment -> seqNum);

        //wprintf(L"%d\n", lastSeqNumRecv);

        //wprintf(L"\nRicevuto pacchetto - (seqNum: %d)\n", atoi(rcvSegment -> seqNum));

        pthread_mutex_lock(&consumeLock);

        /* Calcolo slot finestra e memorizzazione segmento ricevuto e aggiornamento rcvWinFlag */
        slot = normalizeDistance(lastSeqNumRecv, lastAckNumSent);

        /* Primo pacchetto ricevuto dal server contenente filename e dimensione */
        if(firstArrived == 0 && slot == 0) {
            firstArrived = 1;

            tmpStrBuff = intToStr(rcvSegment -> msg, atoi(rcvSegment -> lenMsg));
            // wprintf(L"\nPACCHETTO INIZIALE RICEVUTO: %s, LEN: %d\n", tmpStrBuff, atoi(rcvSegment -> lenMsg));

            tmpStr = strtok(tmpStrBuff, "\b");
            totalSegs = atoi(tmpStr);
            // wprintf(L"\nASPETTO %d SEGMENTI...\n", totalSegs);

            tmpStr = strtok(NULL, "\b");
            strcpy(filename, tmpStr);
            // wprintf(L"\nNOME ORIGINALE: %s...\n", filename);

            tmpStr = strtok(NULL, "\b");
            totalBytes = atoi(tmpStr);
            // wprintf(L"\nASPETTO %d BYTES...\n", totalBytes);

            tmpStr = strtok(NULL, "\b");
            strcpy(originalFileSHA256, tmpStr);
            // wprintf(L"\nSHA256: %s\n", originalFileSHA256);

            free(tmpStrBuff);

            rcvWindow[0] = *rcvSegment;
            rcvWinFlag[0] = 1;

            totalSegsRcv++;

            /* Creazione di un thread per la scrittura dei segmenti su disco */
            if(pthread_create(&consumeTid, NULL, thread_consumeSegment, (void *)filename) != 0)
            {
                wprintf(L"New consumeSegment thread error\n");
                exit(-1);
            }
        } 
        else {
            /* Se il pacchetto ricevuto è valido e non duplicato, ovvero uno dei pacchetti attesi e non un pacchetto già riscontrato e copiato su disco */
            if(slot < WIN_SIZE && rcvWinFlag[slot] == 0) {
                /* Bufferizzo il segmento appena ricevuto */
                rcvWindow[slot] = *rcvSegment;
                rcvWinFlag[slot] = 1;
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
        // sendto(sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket);
        randomSendTo(sockfd, sndSegment, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket, loss_prob);
        //wprintf(L"\nInvio ACK (seqNum: %d) - (ackNum: %d) - (seqNumAcked: %d)\n", atoi(sndSegment -> seqNum), atoi(sndSegment -> ackNum), atoi(rcvSegment -> seqNum));
        free(tmpIntBuff);

        if(totalSegsRcv == totalSegs) {
            pthread_join(consumeTid, NULL);
            //wprintf(L"\nThread consume joinato...\n");
            break;
        }

        if(recvSegment(sockfd, rcvSegment, &operationServerSocket, &addrlenOperationServerSocket) < 0) {
            wprintf(L"\n[DOWNLOAD] -> Server dead. Download FAILED!\n");

            if(consumeTid != -1) {
                pthread_cancel(consumeTid);
                pthread_join(consumeTid, NULL);
            }

            free(sndSegment);
            free(rcvSegment);

            return -1;
        }
    }

    free(sndSegment);
    free(rcvSegment);

    
    return 0;
}

void cpOnFile(FILE *wrFile, char *content, int len) {       
    for(int i = 0; i < len; i++) {
        fputc(content[i], wrFile);
    }
}

void *thread_consumeSegment(void *filename) {
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    int slot;
    int firstArrived = 0;
    int consumedSegments = 0;
    int bytesRecv = 0;
    char *tmpStrBuff;
    char *currentFileSHA256;

    double averageSpeed = 0;
    double lastSpeed;
    int partialBytesRecv = 0;
    double speedElapsTime;
    struct timeval partialDownloadSpeed;
    int oldHalfPercentage = 0;
    gettimeofday(&partialDownloadSpeed, NULL);
    
    wrFile = fopen((char*)filename, "wb");

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
            bytesRecv += atoi(rcvWindow[slot].lenMsg);
            partialBytesRecv += atoi(rcvWindow[slot].lenMsg);
            //wprintf(L"\nScritto su file il segment con seqNum: %d\n", atoi(rcvWindow[slot].seqNum));
            free(tmpStrBuff);
            fflush(wrFile);

            rcvWinFlag[slot] = 0;

            /* Percentuale di segmenti ricevuti */
            consumedSegments++;
            
            /* Corretta */
            wprintf(L"\033[2A\033[2K\033[100D%.2fkB/s\t %.2fkB/s\t %d/%d\033[2B\033[100D", averageSpeed, bytesRecv/elapsedTime(tvDownloadTime), (consumedSegments*100)/(float)(totalSegs-1), bytesRecv, totalBytes);
            printDownloadStatusBar(bytesRecv, totalBytes, &oldHalfPercentage);
            wprintf(L"\033[100D\033[%dC\033[0K%.2f%%", 64, (consumedSegments*100)/(float)(totalSegs-1));

            speedElapsTime = elapsedTime(partialDownloadSpeed);
            if(speedElapsTime > 500) {
                lastSpeed = partialBytesRecv/speedElapsTime;
                if(averageSpeed == 0)
                    averageSpeed = lastSpeed;
                averageSpeed = 0.15*lastSpeed + 0.85*averageSpeed;

                partialBytesRecv = 0;
                gettimeofday(&partialDownloadSpeed, NULL);
            }

            if(atoi(rcvWindow[slot].eotBit) == 1) {

                /* AckNum da inviare al server */
                lastAckNumSent = atoi(rcvWindow[slot].seqNum)%maxSeqNum + 1;

                lastSegmentAcked = atoi(rcvWindow[slot].seqNum);

                /* Sblocco recv_thread per invio ack */
                canSendAck = 1;

                fclose(wrFile);
                wrFile = NULL;

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
            //usleep(500*1000);
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

void *fin_thread(void *args) {
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    signal(SIGINT, ctrl_c_handler);

    int *tmpIntBuff;
    char tmpStrBuff[WIN];
    int countRetransmission = 1;
    
    /* Creazione segmenti di invio/ricezione */
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

    wprintf(L"\n\nDisconnecting from the server... Press CTRL+C to force disconnection\n");

    /* Timer associato al FIN */
    struct timeval finTimeout;
    finTimeout.tv_usec = 0;

    int ret = -1;
    /* Fase di fin 4-way */
    do {
        /* Tenta la ritrasmissione al massimo 10 volte incrementando di volta in volta il timeout */
        if(ret < 0) {
            switch(countRetransmission) {
                case 1:
                    finTimeout.tv_sec = 1;
                    break;

                case 2:
                    finTimeout.tv_sec = 3;
                    break;

                case 11:
                    signal(SIGINT, SIG_DFL);

                    finTimeout.tv_sec = 0;  // Reset del timeout a 0 (= attendi all'infinito)

                    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &finTimeout, sizeof(finTimeout)) < 0) {
                        wprintf(L"\nError while setting fin-timeout at try n°: %d\n", countRetransmission);
                        exit(-1);
                    }

                    free(rcvSegment);
                    free(sndSegment);

                    //blockMain = 0;

                    pthread_exit(NULL);

                    break;

                default:
                    finTimeout.tv_sec = 7;
                    break;
            }

            if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &finTimeout, sizeof(finTimeout)) < 0) {
                wprintf(L"\nError while setting fin-timeout at try n°: %d\n", countRetransmission);
                exit(-1);
            }

            // wprintf(L"\n[FIN] -> Attempt n°%d... - %d\n", countRetransmission, finTimeout.tv_sec);

            // sendto(sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket);
            // if(atoi(isAck) == 1) {
            //     wprintf(L"\n[FIN (ACK)]: Sent to the server (%s:%d)\n", inet_ntoa(operationServerSocket.sin_addr), ntohs(operationServerSocket.sin_port));
            // }
            // else {
            //     wprintf(L"\n[FIN]: Sent to the server (%s:%d)\n", inet_ntoa(operationServerSocket.sin_addr), ntohs(operationServerSocket.sin_port));
            // }
            // /* Invio SYN */
            if(randomSendTo(sockfd, sndSegment, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket, loss_prob))
                if(atoi(isAck)){
                    // wprintf(L"\n[FIN (ACK)]: Sent to the server (%s:%d)\n", inet_ntoa(operationServerSocket.sin_addr), ntohs(operationServerSocket.sin_port));
                }
                else{
                    // wprintf(L"\n[FIN]: Sent to the server (%s:%d)\n", inet_ntoa(operationServerSocket.sin_addr), ntohs(operationServerSocket.sin_port));
                }
            else
                if(atoi(isAck)){
                    // wprintf(L"\n[FIN (ACK)]: LOST\n");
                }
                else{
                    // wprintf(L"\n[FIN]: LOST\n");
                }

            countRetransmission += 1;
        }

        /* Ricezione FIN-ACK */
    } while((ret=recvSegment(sockfd, rcvSegment, &operationServerSocket, &addrlenOperationServerSocket)) < 0 || atoi(rcvSegment -> ackBit) != 1);

    finTimeout.tv_sec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &finTimeout, sizeof(finTimeout)) < 0) {
        wprintf(L"\nError while setting fin-timeout at try n°: %d\n", countRetransmission);
        exit(-1);
    }

    if(atoi(rcvSegment -> finBit) != 1) {
        //wprintf(L"\n\n[FIN]: ACK del FIN ricevuto dal server\n");

        finTimeout.tv_sec = 30; // DA RIMETTERE A "= 30"
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &finTimeout, sizeof(finTimeout)) < 0) {
            wprintf(L"\nError while setting fin-timeout at try n°: %d\n", countRetransmission);
            exit(-1);
        }
        finTimeout.tv_sec = 0;

        /* Ricezione FIN del Server */
        if(recvSegment(sockfd, rcvSegment, &operationServerSocket, &addrlenOperationServerSocket) < 0) {
            if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &finTimeout, sizeof(finTimeout)) < 0) {
                wprintf(L"\nError while setting fin-timeout\n");
                exit(-1);
            }

            wprintf(L"\n[FIN] -> Server unreachable, disconnected...\n");

            signal(SIGINT, SIG_DFL);

            free(sndSegment);
            free(rcvSegment);

            //blockMain = 0;

            pthread_exit(NULL);
        }
        
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &finTimeout, sizeof(finTimeout)) < 0) {
            wprintf(L"\nError while setting fin-timeout\n");
            exit(-1);
        }

        //wprintf(L"\n\n[FIN]: FIN del server ricevuto\n");
    }

    /* Invio ACK del FIN del Server */
    lastAckNumSent = atoi(rcvSegment -> seqNum)%maxSeqNum + 1;
    tmpIntBuff = strToInt(EMPTY);
    newSegment(&sndSegment, FALSE, 2, lastAckNumSent, FALSE, TRUE, FALSE, EMPTY, 1, tmpIntBuff);
    free(tmpIntBuff);
    // sendto(sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket);
    randomSendTo(sockfd, sndSegment, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket, loss_prob);
    //wprintf(L"[FIN]: ACK del FIN inviato al server\n");

    wprintf(L"\n[FIN] -> Succesfully disconnected from Server\n");

    signal(SIGINT, SIG_DFL);

    free(sndSegment);
    free(rcvSegment);

    //blockMain = 0;
    pthread_exit(NULL);
}

void resetData() {
    if (wrFile != NULL){
        fclose(wrFile);
        wrFile = NULL;
    }

    lastSeqNumSent = 0;
    lastAckNumSent = 1;
    totalSegs = WIN_SIZE;
    canSendAck = 0;

    memset(sndWindow, '\0', sizeof(Segment)*WIN_SIZE);
    memset(sndWinFlag, '\0', sizeof(int)*WIN_SIZE);
    memset(rcvWindow, '\0', sizeof(Segment)*WIN_SIZE);
    memset(rcvWinFlag, '\0', sizeof(int)*WIN_SIZE);
}