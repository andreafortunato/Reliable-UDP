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
int upload(FILE*);
void cpOnFile(FILE*, char*, int);
void *thread_consumeSegment(void *);
void *thread_consumeFileList(void *);
void *fin_thread(void*);

void ctrl_c_handler();
void resetData();
void checkIfMustDie(int);

int debug = 0;              /* 1 se l'utente vuole avviare il client in modalità 'Debug' 
                               per visualizzare informazioni aggiuntive, 0 altrimenti */

int sockfd;
int blockMain;
pthread_t finTid;
int renewBind = 0;
int loss_prob = LOSS_PROB;

FILE *wrFile;

char fileToUploadName[256];

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

/* Prototipi e variabili per l'upload */
void *timeout_thread();
void *continuous_send_thread();
void *continuous_recv_thread();

pthread_t sendTid;                      /* ID del Thread Send */
pthread_t recvTid;                      /* ID del Thread Recv */
pthread_t timeTid;                      /* ID del Thread Timeout */
int recvDead;
int mustDie[4];

SegQueue *queueHead;
double RTO;
struct timeval segTimeout[WIN_SIZE];
struct timeval segRtt[WIN_SIZE];
int sendQueue;
int segToSend[WIN_SIZE];
int segSent[WIN_SIZE];
int rttFlag[WIN_SIZE];
int sndWinPos;
int sndPos; 

pthread_mutex_t queueLock;
pthread_rwlock_t slideLock; 

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
                wprintf(L"File to download: ");
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
                /* Comando upload */
                wprintf(L"File to upload (write file name or full path): ");
                scanf("%s", fileToUploadName);

                FILE *fileToUpload = fopen(fileToUploadName, "rb");
                if(fileToUpload == NULL){
                    wprintf(L"[ERROR] File not found. Check the file name (it is Case Sensitive!)\n");
                    break;
                }

                /* Fase di handshake */
                if(handshake() == -1) {
                    wprintf(L"\nError during handshake phase\n");
                    break;  
                } 

                /* Operazione upload */
                if(upload(fileToUpload) == -1) {
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
        // while(blockMain == 1);
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

int upload(FILE *fileToUpload) {
    Segment *sndSegment = NULL;
    int *tmpIntBuff;

    int ret;
    int *joinRet;

    fseek(fileToUpload, 0, SEEK_END);
    int fileLen = ftell(fileToUpload);
    fseek(fileToUpload, 0, SEEK_SET);

    /* Formula della vita: (A-1)/B+1, parte intera superiore della divisione A/B */
    int totalSegs = (fileLen-1)/(LEN_MSG) + 1;

    ret = pthread_create(&timeTid, NULL, timeout_thread, NULL);
    if(ret != 0)
    {
        wprintf(L"New timeout thread error\n");
        exit(-1);
    }
    ret = pthread_create(&sendTid, NULL, continuous_send_thread, NULL);
    if(ret != 0)
    {
        wprintf(L"New continuous send thread error\n");
        exit(-1);
    }
    ret = pthread_create(&recvTid, NULL, continuous_recv_thread, NULL);
    if(ret != 0)
    {
        wprintf(L"New continuous recv thread error\n");
        exit(-1);
    }

    int buffFile[LEN_MSG];
    int ch;
    int i, j;
    int currentPos = 1; 

    /* Caricamento del primo pacchetto contenente \bTOTALSEGS\bNOME\bLUNGHEZZA_IN_BYTE\bCODICE_SHA256 */
    char tmpStrBuff[LEN_MSG];
    char *fileSHA256 = getFileSHA256(fileToUploadName);


    sprintf(tmpStrBuff, "\b%d\b%s\b%d\b%s\b", totalSegs+1, fileToUploadName, fileLen, fileSHA256);
    tmpIntBuff = strToInt(tmpStrBuff);
    newSegment(&sndSegment, FALSE, 1, -1, FALSE, FALSE, FALSE, "3", strlen(tmpStrBuff), tmpIntBuff);

    sndWindow[sndWinPos] = *sndSegment;
    segToSend[sndWinPos] = 1;
    sndWinPos++;

    free(tmpIntBuff);

    /* Caricamento segmenti da inviare nella finestra sndWindow */
    for(i=1; i<=totalSegs; i++) {
        // checkIfMustDie(HANDSHAKE);
        
        /* Lettura di LEN_MSG caratteri convertiti in formato network e memorizzati in buffFile (Endianness problem) */
        bzero(buffFile, LEN_MSG);
        for(j = 1; j <= LEN_MSG; j++, currentPos++) {
            if((ch = fgetc(fileToUpload)) == EOF)
                break;
            buffFile[j-1] = htonl(ch);
        }

        /* Se è l'ultimo segmento imposta End-Of-Transmission-Bit a 1 */
        if(i == totalSegs)
            newSegment(&sndSegment, TRUE, (i%maxSeqNum)+1, -1, FALSE, FALSE, FALSE, "3", j-1, buffFile);
        else
            newSegment(&sndSegment, FALSE, (i%maxSeqNum)+1, -1, FALSE, FALSE, FALSE, "3", j-1, buffFile);

        /* Attendi slide finestra per il caricamento di altri segmenti */
        while(sndWinPos >= WIN_SIZE) {
            // checkIfMustDie(HANDSHAKE);
            
            if(recvDead) {
                fclose(fileToUpload);
                
                pthread_join(recvTid, (void**)&joinRet);
                wprintf(L"VALORE RITORNATO DALLA JOIN: %d\n", *((int*)joinRet));
                if(*((int*)joinRet) == -1)
                    return -1;
            }
        }

        pthread_rwlock_rdlock(&slideLock);

        sndWinPos++;

        sndWindow[sndWinPos-1] = *sndSegment;
        segToSend[sndWinPos-1] = 1;

        // wprintf(L"\n[MAIN] -> Caricato pacchetto: (seqNum: %d) - (sndWinPos: %d)\n", atoi(sndSegment -> seqNum), sndWinPos-1);

        if(pthread_rwlock_unlock(&slideLock) != 0) {
            wprintf(L"%d : %s\n", errno, strerror(errno));
            exit(-1);
        }
    }

    //wprintf(L"Fine caricamento pacchetti\n");
    fclose(fileToUpload);

    pthread_join(recvTid, (void**)&joinRet);
    if(*joinRet == -1)
        return -1;

    return 0;
}

void *timeout_thread() {
    
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    int timeoutPos = 0;    
    int maxSeqNumSendable;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    tv.tv_sec += 86400; // Per impostare un timeout molto elevato, nel "futuro"

    while(1) {
        checkIfMustDie(TIMEOUT);

        pthread_rwlock_rdlock(&slideLock);

        /* Se il segmento sndWindow[timeoutPos] è stato inviato allora controlla il suo timeout */
        if(segSent[timeoutPos] == 1) {
            /* Se il timeout per il segmento in esame è scaduto */
            if(elapsedTime(segTimeout[timeoutPos]) > 100) {
                
                pthread_mutex_lock(&queueLock);

                /*************************************************************************************************************************/
                // wprintf(L"\n[TIMEOUT] -> Timeout scaduto: (seqNum: %d) - (timeoutPos: %d)\n", atoi(sndWindow[timeoutPos].seqNum), timeoutPos);
                /**************************************************************************************************************************/

                segTimeout[timeoutPos] = tv;

                //wprintf(L"sndWindow[0].seqNum: %s - WIN_SIZE: %d - maxSeqNum: %d ", sndWindow[0].seqNum, WIN_SIZE, maxSeqNum);
                maxSeqNumSendable = (atoi(sndWindow[0].seqNum) + WIN_SIZE-2)%(maxSeqNum) + 1;
                //wprintf(L"SNDWINDOW[0]: %s - maxSeqNumSendable: %d - ", sndWindow[0].seqNum, maxSeqNumSendable);
                orderedInsertSegToQueue(&queueHead, sndWindow[timeoutPos], timeoutPos, maxSeqNumSendable);

                if(pthread_mutex_unlock(&queueLock) != 0) {
                    wprintf(L"%d : %s\n", errno, strerror(errno));
                    exit(-1);
                }
            }
        }

        timeoutPos = (timeoutPos+1) % WIN_SIZE;

        if(pthread_rwlock_unlock(&slideLock) != 0) {
            wprintf(L"%d : %s\n", errno, strerror(errno));
            exit(-1);
        }
    }
}

void *continuous_send_thread() {

    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    
    SegQueue *prev;

    while(1) {
        checkIfMustDie(SEND);
        /* Attesa sblocco coda di trasmissione da parte del thread recv */
        if(sendQueue == 0)
            continue;

        pthread_rwlock_rdlock(&slideLock);

        /* Invio dell'intera coda */
        while(queueHead != NULL && sendQueue == 1) {
            checkIfMustDie(SEND);

            pthread_mutex_lock(&queueLock);

            //wprintf(L"\n[SEND] -> Invio pacchetto in coda: (seqNum: %d) - (winPos: %d)\n", atoi((queueHead -> segment).seqNum), queueHead -> winPos);
            /*************************************************************************************************************************/
            //wprintf(L"\nsegTimeout[%d]: %ld\n", queueHead -> winPos, segTimeout[queueHead -> winPos].tv_sec);
            /**************************************************************************************************************************/
            if(randomSendTo(sockfd, &(queueHead -> segment), (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket, loss_prob) == 1) {
                // wprintf(L"\n[RAND_SENDTO-QUEUE] -> pacchetto inviato seqNum: "); //wprintf(L"%d\n", atoi((queueHead -> segment).seqNum));
                // if(queueHead == NULL) {
                //     wprintf(L"queueHead NULL\n");
                //     exit(-1);
                // }else {
                //     wprintf(L"%d\n", atoi((queueHead -> segment).seqNum));
                // }
            }
            else {
                // wprintf(L"\n[RAND_SENDTO-QUEUE] -> pacchetto perso seqNum: "); wprintf(L"%d\n", atoi((queueHead -> segment).seqNum));
            }
            /**************************************************************************************************************************/

            /* Imposta il nuovo timestamp */
            gettimeofday(&segTimeout[queueHead -> winPos], NULL);

            /* Controllo se il segmento queueHead -> segment è già stato riscontrato allora 
               resettiamo il timestamp per il calcolo di un nuovo rtt */
            if(rttFlag[queueHead -> winPos] == 1) {
                gettimeofday(&segRtt[queueHead -> winPos], NULL);
                rttFlag[queueHead -> winPos] = 0;
            }
            
            prev = queueHead;
            queueHead = queueHead -> next;
            free(prev);

            if(pthread_mutex_unlock(&queueLock) != 0) {
                wprintf(L"%d : %s\n", errno, strerror(errno));
                exit(-1);
            }
        }
       
        /* (Primo controllo) Se la recv ha ricevuto un ACK 'corretto', interrompe l'invio della coda (sendQueue=0)
           e resetta lo stato della send;
           (Secondo controllo) Se ho inviato tutta la finestra (ovvero tutti i segToSend sono uguali a 0) resetto 
           lo stato della send */
        if(sendQueue == 0 || segToSend[sndPos] == 0) {
            if(pthread_rwlock_unlock(&slideLock) != 0) {
                wprintf(L"%d : %s\n", errno, strerror(errno));
                exit(-1);
            }
            continue;
        }

        /* Impostiamo il timestamp per il calcolo dell'rtt */
        gettimeofday(&segRtt[sndPos], NULL);
        
        if(randomSendTo(sockfd, &(sndWindow[sndPos]), (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket, loss_prob) == 1) {
            // wprintf(L"\n[RAND_SENDTO] -> pacchetto inviato seqNum: %d - (sndPos: %d)\n", atoi(sndWindow[sndPos].seqNum), sndPos);
        }
        else {
            // wprintf(L"\n[RAND_SENDTO] -> pacchetto perso seqNum: %d - (sndPos: %d)\n", atoi(sndWindow[sndPos].seqNum), sndPos);
        }
        
        /* Impostiamo il timestamp per il calcolo del timeout */
        gettimeofday(&segTimeout[sndPos], NULL);

        segToSend[sndPos] = 0;
        segSent[sndPos] = 1;

        sndPos = (sndPos+1) % WIN_SIZE;

        if(pthread_rwlock_unlock(&slideLock) != 0) {
            wprintf(L"%d : %s\n", errno, strerror(errno));
            exit(-1);
        }
    }
}

void *continuous_recv_thread(){
    
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    
    RTT_Data *rttData = initData();

    Segment *rcvSegment = (Segment*)malloc(sizeof(Segment));
    if(rcvSegment == NULL)
    {
        wprintf(L"Error while trying to \"malloc\" a new rcvSegment!\nClosing...\n");
        exit(-1);
    }

    int *joinRet = malloc(1*sizeof(int));

    int lastAck = 1;
    int countAck = 0;
    int rcvAck;
    int rcvAckedSeq;
    int slideSize;
    int rttPos;
    int tmpPos;
    char *tmpStrBuff;
    SegQueue *head;

    struct timeval deadLineTimeout;
    struct timeval repeatTimeout;
    repeatTimeout.tv_sec = 0;
    repeatTimeout.tv_usec = 100*1000;

    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &repeatTimeout, sizeof(repeatTimeout)) < 0) {
        wprintf(L"\nError while setting repeatTimeout-timeout\n");
        exit(-1);
    }

    gettimeofday(&deadLineTimeout, NULL);
    while(1) {
        checkIfMustDie(RECV);
        if(recvSegment(sockfd, rcvSegment, &operationServerSocket, &addrlenOperationServerSocket) < 0) {
            if(elapsedTime(deadLineTimeout) > 60*1000) {
                wprintf(L"RECV FALLITA\n");

                // Free della struttura dati thread
                *joinRet = -1;
                wprintf(L"Ora termino\n");
                recvDead = 1;
                pthread_exit(joinRet);
            }
            else
                continue;
        }
        gettimeofday(&deadLineTimeout, NULL);

        tmpStrBuff = intToStr(rcvSegment -> msg, atoi(rcvSegment -> lenMsg));
        rcvAck = atoi(rcvSegment -> ackNum);
        rcvAckedSeq = atoi(tmpStrBuff);
        // wprintf(L"\n[RECV] -> Ricevuto ACK: (seqNum: %d) - (ackNum: %d) - (seqNumAcked: %d)\n", atoi(rcvSegment -> seqNum), rcvAck, rcvAckedSeq);
        free(tmpStrBuff);

        /* Se l'ack ricevuto ha il fin bit impostato ad 1 allora il server ha ricevuto tutto e
           ha chiesto la chiusura della connessione */
        if(atoi(rcvSegment -> finBit) == 1) {
            //wprintf(L"\n[RECV] -> Ricevuto pacchetto di FIN: (seqNum: %d) - (ackNum: %d)\n", atoi(rcvSegment -> seqNum), rcvAck);
            checkIfMustDie(RECV);
            
            // Free della struttura dati thread
            *joinRet = 0;
            pthread_exit(joinRet);
        }

        /* Se c'è almeno un segmento caricato processa l'ack */
        if(sndWinPos != 0) {

            /* Aggiornamento dell'RTO */
            rttPos = normalizeDistance(rcvAckedSeq, atoi(sndWindow[0].seqNum));
            if(rttPos < WIN_SIZE) {
                //wprintf(L"\nRTT_FLAG[%d] = %d - %d\n", rttPos, rttFlag[rttPos], atoi(sndWindow[0].seqNum));
                if(rttFlag[rttPos] == 0) {
                    RTO = calculateRTO(segRtt[rttPos], rttData);
                    rttFlag[rttPos] = 1;
                }
            }

            /* Calcolo della dimensione dell'eventuale slide da effettuare */
            slideSize = normalize(rcvAck, atoi(sndWindow[0].seqNum));

            if(rcvAck == atoi(sndWindow[0].seqNum) || slideSize > WIN_SIZE) {
                //wprintf(L" - Ack non valido\n");
                if(rcvAck == lastAck) {
                    /* Meccanismo del Fast Retransmit: aggiunta del segmento in testa alla coda di ritrasmissione */
                    if(++countAck == 3) {
                        pthread_rwlock_wrlock(&slideLock);
                        //wprintf(L"\n[RECV] -> Fast Retransmit for seqNum: %d\n", rcvAck);
                        countAck = 0;
                        if(queueHead == NULL || atoi((queueHead -> segment).seqNum) != rcvAck) {
                            sendQueue = 0; // Stoppiamo il send durante la trasmissione della coda
                            
                            head = queueHead;
                            tmpPos = 0;
                            while(atoi(sndWindow[tmpPos++].seqNum) != rcvAck);
                            queueHead = newSegQueue(sndWindow[tmpPos-1], tmpPos-1);
                            queueHead -> next = head;
                            sendQueue = 1; // Riavviamo il send durante la trasmissione della coda
                        }
                        if(pthread_rwlock_unlock(&slideLock) != 0) {
                            wprintf(L"%d : %s\n", errno, strerror(errno));
                            exit(-1);
                        }
                    }
                } else {
                    lastAck = rcvAck;
                    countAck = 1;
                }
            }
            /* Se l'ACK ricevuto è valido, allora permette uno slide della finestra (rientra tra sndWindow[1].seqNum, ..., sndWindow[WIN_SIZE-1].seqNum) */ 
            else {
                sendQueue = 0; // Stoppiamo il send durante la trasmissione della coda
                pthread_rwlock_wrlock(&slideLock);
                //wprintf(L" - Ack valido\n");

                lastAck = rcvAck;
                countAck = 1;

                /* Eliminazione dalla coda di tutti i segmenti più vecchi rispetto all'ack appena ricevuto */
                int maxSeqNumSendable = (atoi(sndWindow[0].seqNum) + WIN_SIZE-2)%(maxSeqNum) + 1;
                int distance = normalizeDistance(maxSeqNumSendable, rcvAck);
                distance = distance >= WIN_SIZE ? WIN_SIZE-1 : distance;
                // wprintf(L"Slide: %d\n", slideSize);
                SegQueue *current = queueHead;
                SegQueue *prev = queueHead;
                while(current != NULL) {
                    prev = current;
                    current = current -> next;
                    if(isSeqMinor(rcvAck, atoi((prev -> segment).seqNum), distance)) {
                        deleteSegFromQueue(&queueHead, prev);
                    }
                    else
                        prev -> winPos -= slideSize;
                }

                /* Se lo slide da effettuare è totale allora resetta tutte le strutture dati flag */  
                if(slideSize == WIN_SIZE) {
                    memset(&segToSend[0], 0, sizeof(int)*slideSize);
                    memset(&segSent[0], 0, sizeof(int)*slideSize);
                    memset(&rttFlag[0], 0, sizeof(int)*slideSize);                    
                }
                else {
                    /* Slide di tutte le strutture dati */
                    // Crashava qui, il comando LIST
                    memmove(segToSend, &segToSend[slideSize], sizeof(int)*(WIN_SIZE-slideSize));
                    memset(&segToSend[WIN_SIZE-slideSize], 0, sizeof(int)*slideSize);

                    memmove(segSent, &segSent[slideSize], sizeof(int)*(WIN_SIZE-slideSize));
                    memset(&segSent[WIN_SIZE-slideSize], 0, sizeof(int)*slideSize);

                    memmove(rttFlag, &rttFlag[slideSize], sizeof(int)*(WIN_SIZE-slideSize));
                    memset(&rttFlag[WIN_SIZE-slideSize], 0, sizeof(int)*slideSize);

                    memmove(segTimeout, &segTimeout[slideSize], sizeof(struct timeval)*(WIN_SIZE-slideSize));

                    memmove(segRtt, &segRtt[slideSize], sizeof(struct timeval)*(WIN_SIZE-slideSize));

                    memmove(sndWindow, &sndWindow[slideSize], sizeof(Segment)*(WIN_SIZE-slideSize));                    
                }

                /* Se si era arrivati all'invio di un pacchetto in posizione X
                   ed è stato fatto uno slide di Y allora riprendi il caricamento/invio 
                   dalla posizione X-Y */
                sndWinPos -= slideSize;
                if(sndPos != 0){
                    sndPos -= slideSize;
                } else {
                    sndPos = sndWinPos;
                }

                sendQueue = 1; // Riavviamo il send durante la trasmissione della coda

                if(pthread_rwlock_unlock(&slideLock) != 0) {
                    wprintf(L"%d : %s\n", errno, strerror(errno));
                    exit(-1);
                }
            }
        }
        //else wprintf(L" - ACK IGNORATO\n");
    }
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

void checkIfMustDie(int threadNum) {
    if(mustDie[threadNum])
        pthread_exit(NULL);
}