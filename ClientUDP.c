/* 
    Client - FTP on UDP 
    
    Authors: Enrico D'Alessandro & Andrea Fortunato
    IIW (A.Y. 2019-2020) at Università di Tor Vergata in Rome.
*/

#include <wchar.h>
#include <locale.h>
#include "Client.h"
  
#define IP_PROTOCOL 0       /* Protocollo UDP default */

typedef struct sockaddr_in Sockaddr_in;

/******************************************************* PROTOTIPI ********************************************************************/
int handshake();                        /* Funzione per la gestione della fase di handshake */
int list();                             /* Funzione per la gestione del comando list */
int download(char*);                    /* Funzione per la gestione del comando download */
int upload(FILE*);                      /* Funzione per la gestione del comando upload */
void ctrl_c_handler();                  /* Funzione per la gestione del ctrl_c utilizzato durante la fase di fin */
void cpOnFile(FILE*, char*, int);       /* Funzione per la copia dei segmenti su file durante il comando List o Download */
void resetData();                       /* Funzione per il reset delle variabili tra un'operazione e l'altra */
void checkIfMustDie(int);               /* Funzione per il controllo della terminazione dei thread */

void *thread_consumeSegment(void *);    /* Thread per la consumazione dei segmenti durante operazione Download */
void *thread_consumeFileList();   /* Thread per la consumazione dei segmenti durante operazione List */
void *fin_thread();                /* Thread per la gestione della fase di fin */
void *timeout_thread();                 /* Thread per la gestione del timeout dei segmenti durante l'upload */
void *continuous_send_thread();         /* Thread per l'invio dei segmenti durante l'upload */
void *continuous_recv_thread();         /* Thread per la ricezione degli ACK durante l'upload */
    
/**************************************************************************************************************************************/

/************************************************** VARIABILI GLOBALI *****************************************************************/
int debug = 0;              /* 1 se l'utente vuole avviare il client in modalità 'Debug' 
                               per visualizzare informazioni aggiuntive, 0 altrimenti */

int sockfd;                 /* Descrittore socket utilizzato dal client */
pthread_t finTid;           /* Tid del thread fin */
pthread_t recvTid;          /* Tid del thread recv */
pthread_t sendTid;          /* Tid del thread send */
pthread_t timeTid;          /* Tid del thread timeout */
int loss_prob = LOSS_PROB;  /* Variabile utilizzata per il cambio a run-time della probabilità di perdita */

FILE *wrFile;               /* Puntatore a file per la scrittura su disco */

char fileToUploadName[256]; /* Nome del file da caricare sul server */

int lastSeqNumSent;         /* Numero di sequenza dell'ultimo segmento inviato */
int lastSeqNumRecv;         /* Numero di sequenza dell'ultimo segmento ricevuto */
int lastAckNumSent;         /* Numero di sequenza dell'ultimo ACK inviato */
int lastSegmentAcked;       /* Numero di sequenza dell'ultimo segmento riscontrato */
int totalSegs;              /* Numero totale di segmenti attesi */
int totalBytes;             /* Numero totale di bytes attesi */
int canSendAck;
struct timeval tvDownloadTime;
char originalFileSHA256[65];/* Calcolo del sha256sum del file di cui fare l'upload */
char isAck[BIT];

Segment sndWindow[WIN_SIZE];/* Finestra di invio */ 
Segment rcvWindow[WIN_SIZE];/* Finestra di ricezione */
int rcvWinFlag[WIN_SIZE];   /* 1 se il segmento i-esimo è stato ricevuto in finestra e quindi scrivibile, 0 altrimenti */
int maxSeqNum;              /* Massimo numero di sequenza inviabile = WIN_SIZE*2 */

pthread_mutex_t consumeLock;/* Lock per consetire la scrittura su disco dei segmenti ricevuti */

/* Sockaddr_in server, utilizzata per l'handshake e le richieste di operazioni */
Sockaddr_in mainServerSocket;
int addrlenMainServerSocket;

/* Sockaddr_in server, utilizzata per l'esecuzione delle operazioni */
Sockaddr_in operationServerSocket;
int addrlenOperationServerSocket;

pthread_t sendTid;                      /* ID del Thread Send */
pthread_t recvTid;                      /* ID del Thread Recv */
pthread_t timeTid;                      /* ID del Thread Timeout */
int recvDead;
int mustDie[4];

SegQueue *queueHead;                    /* Puntatore alla testa della coda di ritrasmissione */
double RTO;                             /* RTO variabile */
struct timeval segTimeout[WIN_SIZE];    /* Finestra per controllare il timeout dei segmenti inviati */
struct timeval segRtt[WIN_SIZE];        
int sendQueue;
int segToSend[WIN_SIZE];                /* 1 se il segmento i-esimo è stato caricato in finestra e quindi inviabile, 0 altrimenti */
int segSent[WIN_SIZE];                  /* 1 per contrassegnare un segmento come inviato, 0 altrimenti */
int rttFlag[WIN_SIZE];                  /* 1 se RTT per l'i-esimo segmento è già stato calcolato, 0 altrimenti */
int sndWinPos;
int sndPos; 

pthread_mutex_t queueLock;              /* Mutex per l'accesso alla coda di ritrasmissione */
pthread_rwlock_t slideLock;             /* Semaforo R/W per consentire lo slide delle finestre */
/**************************************************************************************************************************************/


/* Main - Client */
int main(int argc, char *argv[]) { 

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
        wprintf(ERROR L"Failed malloc on ip address failed!\n");
        exit(-1);
    }

    /* Parse dei parametri passati da riga di comando */
    int port = parseCmdLine(argc, argv, "client", &ip, &debug); 
    if(port == -1)
    {
        exit(0);
    }

    if(pthread_mutex_init(&consumeLock, NULL) != 0) {
        wprintf(ERROR L"Failed consumeLock semaphore initialization.\n");
        exit(-1);
    }

    maxSeqNum = WIN_SIZE*2;

    mainServerSocket.sin_family = AF_INET;
    mainServerSocket.sin_addr.s_addr = inet_addr(ip);
    mainServerSocket.sin_port = htons(port); 
    //mainServerSocket.sin_addr.s_addr = inet_addr("127.0.0.1");
    //mainServerSocket.sin_port = htons(47435); 
    addrlenMainServerSocket = sizeof(mainServerSocket);
    addrlenOperationServerSocket = sizeof(operationServerSocket);

    /* Creazione socket - UDP */
    sockfd = socket(AF_INET, SOCK_DGRAM, IP_PROTOCOL);
    if (sockfd < 0) {
        wprintf(L"\n" ERROR L"Socket file descriptor not received!\n");
        exit(-1);
    }

    Segment *rcvSegment = (Segment*)malloc(sizeof(Segment));
    if(rcvSegment == NULL)
    {
        wprintf(ERROR L"Error while trying to \"malloc\" a new Segment!\nClosing...\n");
        exit(-1);
    }

    /* Eventuale 'pulizia' del buffer, in caso siano rimasti segmenti pendenti in rete
       precedentemente inviati sulla stessa porta sulla quale è appena stata aperta la socket */
    struct timeval clearInBuffer;
    clearInBuffer.tv_sec = 0;

    while (1) {

        if(debug) wprintf(L"\n\n" INFO "PID: %d", getpid());
        
        choice = clientChoice();

        resetData();

        clearInBuffer.tv_usec = 50*1000;
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &clearInBuffer, sizeof(clearInBuffer)) < 0) {
            wprintf(L"\n" ERROR "While setting clearInBuffer\n");
            exit(-1);
        }
        while(recvSegment(sockfd, rcvSegment, &operationServerSocket, &addrlenOperationServerSocket) >= 0);
        clearInBuffer.tv_usec = 0;
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &clearInBuffer, sizeof(clearInBuffer)) < 0) {
            wprintf(L"\n" ERROR "While setting clearInBuffer\n");
            exit(-1);
        }

        switch(choice) {

            case 1:
                /* Fase di handshake */
                if(handshake() == -1) {
                    wprintf(L"\n" ERROR "During handshake phase\n");
                    break;  
                } 

                /* Operazione list */
                if(list() == -1) {
                    break;
                }

                /* Fase di chiusura */
                strcpy(isAck, TRUE);
                if(pthread_create(&finTid, NULL, fin_thread, NULL) != 0)
                {
                    wprintf(ERROR L"New client thread error\n");
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
                    wprintf(L"\n" ERROR "During handshake phase\n");
                    break;  
                } 

                /* Operazione download */
                if(download(filename) == -1) {
                    break;
                }

                /* Fase di chiusura */
                strcpy(isAck, TRUE);
                if(pthread_create(&finTid, NULL, fin_thread, NULL) != 0)
                {
                    wprintf(ERROR L"New client thread error\n");
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
                    wprintf(ERROR L"File not found. Check the file name (it is Case Sensitive!)\n");
                    break;
                }

                /* Fase di handshake */
                if(handshake() == -1) {
                    wprintf(L"\n" ERROR "During handshake phase\n");
                    break;  
                } 

                /* Operazione upload */
                if(upload(fileToUpload) == -1) {
                    mustDie[TIMEOUT] = 1;
                    mustDie[SEND] = 1;
                    pthread_join(timeTid, NULL);
                    pthread_join(sendTid, NULL);
                    break;
                }

                wprintf(L"\n" INFO "Upload completed successfully!\n");
                
                /* Fase di chiusura */
                strcpy(isAck, FALSE);
                if(pthread_create(&finTid, NULL, fin_thread, NULL) != 0)
                {
                    wprintf(ERROR L"New client thread error\n");
                    exit(-1);
                }
                pthread_join(finTid, NULL);

                break;

            case 4:
                /* Uscita dal programma */
                if(pthread_mutex_destroy(&consumeLock) != 0) {
                    wprintf(ERROR L"Failed consumeLock semaphore destruction.\n");
                    exit(-1);
                }

                exit(0);
                break;
        }
    } 

    return 0; 
}

/* Funzione per la terminazione tramite ctrl_c della fase di fin */
void ctrl_c_handler() {
    signal(SIGINT, SIG_DFL);
    pthread_cancel(finTid);

    struct timeval resetTimeout;
    resetTimeout.tv_usec = 0;
    resetTimeout.tv_sec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &resetTimeout, sizeof(resetTimeout)) < 0) {
        wprintf(L"\n" ERROR "While setting reset-timeout\n");
        exit(-1);
    }
}

/* Funzione per la gestione della fase di handshake */
int handshake() {
    wprintf(L"\nWaiting for connection to the server...\n");

    int *tmpIntBuff;
    int countRetransmission = 1;
    int ret = -1;

    tmpIntBuff = strToInt(EMPTY);

    /* Creazione segmenti di invio/ricezione */
    Segment *sndSegment = NULL;
    newSegment(&sndSegment, FALSE, 1, -1, TRUE, FALSE, FALSE, EMPTY, 1, tmpIntBuff);
    free(tmpIntBuff);

    Segment *rcvSegment = (Segment*)malloc(sizeof(Segment));
    if(rcvSegment == NULL)
    {
        wprintf(ERROR L"While trying to \"malloc\" a new handshake segment!\nClosing...\n");
        exit(-1);
    } 
    bzero(rcvSegment, sizeof(Segment));   

    /* Timer associato al SYN */
    struct timeval synTimeout;
    synTimeout.tv_usec = 0;

    /* Fase di handshake 3-way */
    do {
        if(ret<0) {
            /* Tenta la ritrasmissione al massimo 10 volte incrementando di volta in volta il timeout */
            switch(countRetransmission) {
                case 1:
                    synTimeout.tv_sec = 1;
                    break;

                case 2:
                    synTimeout.tv_sec = 3;
                    break;

                case 11:
                    synTimeout.tv_sec = 0;  /* Reset del timeout a 0 (= attendi all'infinito) */

                    free(rcvSegment);
                    free(sndSegment);

                    return -1;
                    break;

                default:
                    synTimeout.tv_sec = 7;
                    break;
            }

            if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &synTimeout, sizeof(synTimeout)) < 0) {
                wprintf(L"\n" ERROR "While setting syn-timeout at try n°: %d\n", countRetransmission);
                exit(-1);
            }
        }

        /* Invio SYN */
        if(randomSendTo(sockfd, sndSegment, (struct sockaddr*)&mainServerSocket, addrlenMainServerSocket, loss_prob)){
            if(debug) wprintf(L"\n" HAND "[SYN]: Sent to the server (%s:%d)\n", inet_ntoa(mainServerSocket.sin_addr), ntohs(mainServerSocket.sin_port));
        }
        else{
            if(debug) wprintf(L"\n" HAND "[SYN]: LOST\n");
        }

        countRetransmission += 1;

        /* Ricezione SYN-ACK */
    } while((ret = recvSegment(sockfd, rcvSegment, &operationServerSocket, &addrlenOperationServerSocket)) < 0 || atoi(rcvSegment -> synBit) != 1 || atoi(rcvSegment -> ackBit) != 1);

    synTimeout.tv_sec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &synTimeout, sizeof(synTimeout)) < 0) {
        wprintf(L"\n" ERROR "While setting syn-timeout at try n°: %d\n", countRetransmission);
        exit(-1);
    }

    /* SYN-ACK ricevuto */
    if(debug) wprintf(HAND L"[SYN-ACK]: Server information: (%s:%d)\n", inet_ntoa(operationServerSocket.sin_addr), ntohs(operationServerSocket.sin_port));
    
    /* Invio ACK del SYN-ACK */
    int ackNum = atoi(rcvSegment -> seqNum) + 1;
    tmpIntBuff = strToInt(EMPTY);
    newSegment(&sndSegment, FALSE, 2, ackNum, FALSE, TRUE, FALSE, EMPTY, 1, tmpIntBuff);
    free(tmpIntBuff);
    
    if(randomSendTo(sockfd, sndSegment, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket, loss_prob)){
        if(debug) wprintf(HAND L"[ACK]: Sent to the server (%s:%d)\n", inet_ntoa(operationServerSocket.sin_addr), ntohs(operationServerSocket.sin_port));
    }
    else {
        if(debug) wprintf(L"\n" HAND "[ACK]: LOST\n");
    }
    
    free(rcvSegment);
    free(sndSegment);

    /* Fine handshake */
    if(debug) wprintf(INFO L"Handshake terminated!\n\n");
    return 0;
}

/* Funzione per la gestione del comando list */
int list() {

    int *tmpIntBuff;
    char *tmpStrBuff;
    char *tmpStr;
    int countRetransmission = 1;
    int slot;
    int ret = -1;
    int totalSegsRcv = 0;
    totalSegs = WIN_SIZE;
    int firstArrived = 0;
    pthread_t consumeTid = -1;

    Segment *sndSegment = NULL;
    Segment *rcvSegment = (Segment*)malloc(sizeof(Segment));
    if(rcvSegment == NULL)
    {
        wprintf(ERROR L"While trying to \"malloc\" a new list rcvsegment!\nClosing...\n");
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
        if(ret<0) {
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
                        wprintf(L"\n" ERROR "While setting fin-timeout at try n°: %d\n", countRetransmission);
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
                wprintf(L"\n" ERROR "While setting fin-timeout at try n°: %d\n", countRetransmission);
                exit(-1);
            }
        }
        
        /* Invio SYN */
        if(randomSendTo(sockfd, sndSegment, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket, loss_prob)) {
            if(debug) wprintf(L"\n" LIST "Request sent to the server (%s:%d)\n", inet_ntoa(operationServerSocket.sin_addr), ntohs(operationServerSocket.sin_port));
        }
        else {
            if(debug) wprintf(L"\n" LIST "Request operation List lost.\n");
        }

        countRetransmission += 1;
    } while((ret = recvSegment(sockfd, rcvSegment, &operationServerSocket, &addrlenOperationServerSocket)) < 0 || atoi(rcvSegment -> synBit) == 1);

    requestTimeout.tv_sec = 64;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &requestTimeout, sizeof(requestTimeout)) < 0) {
        wprintf(L"\n" ERROR "While setting fin-timeout at try n°: %d\n", countRetransmission);
        exit(-1);
    }

    while(1) {

        lastSeqNumRecv = atoi(rcvSegment -> seqNum);

        if(debug) wprintf(L"\n" LIST" Ricevuto pacchetto - (seqNum: %d)\n", atoi(rcvSegment -> seqNum));

        pthread_mutex_lock(&consumeLock);

        /* Calcolo slot finestra e memorizzazione segmento ricevuto e aggiornamento rcvWinFlag */
        slot = normalizeDistance(lastSeqNumRecv, lastAckNumSent);

        /* Primo pacchetto ricevuto dal server contenente filename e dimensione */
        if(firstArrived == 0 && slot == 0) {
            firstArrived = 1;

            tmpStrBuff = intToStr(rcvSegment -> msg, atoi(rcvSegment -> lenMsg));
            tmpStr = strtok(tmpStrBuff, "\b");
            totalSegs = atoi(tmpStr);

            free(tmpStrBuff);

            rcvWindow[0] = *rcvSegment;
            rcvWinFlag[0] = 1;

            totalSegsRcv++;

            /* Creazione di un thread per la scrittura dei segmenti su disco */
            if(pthread_create(&consumeTid, NULL, thread_consumeFileList, NULL) != 0)
            {
                wprintf(ERROR L"New consumeSegment thread error\n");
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

        /* Se è consumabile aspetto che il consume scriva su file */
        if(rcvWinFlag[0] == 1) {
            while(canSendAck == 0); /* Attendi che il consume abbia consumato e aggiornato lastAckNumSent */
            canSendAck = 0;
        }

        lastSeqNumSent = lastSeqNumSent%maxSeqNum + 1;

        tmpIntBuff = strToInt(rcvSegment -> seqNum);
        newSegment(&sndSegment, FALSE, lastSeqNumSent, lastAckNumSent, FALSE, TRUE, FALSE, "1", strlen(rcvSegment -> seqNum), tmpIntBuff);
        randomSendTo(sockfd, sndSegment, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket, loss_prob);
        if(debug) wprintf(L"\n" LIST "Invio ACK (seqNum: %d) - (ackNum: %d) - (seqNumAcked: %d)\n", atoi(sndSegment -> seqNum), atoi(sndSegment -> ackNum), atoi(rcvSegment -> seqNum));
        free(tmpIntBuff);

        if(totalSegsRcv == totalSegs) {
            pthread_join(consumeTid, NULL);
            break;
        }

        if(recvSegment(sockfd, rcvSegment, &operationServerSocket, &addrlenOperationServerSocket) < 0) {
            wprintf(L"\n" ERROR "Server dead. List FAILED!\n");

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

/* Thread per la consumazione dei segmenti durante operazione List */
void *thread_consumeFileList() {

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

    lastAckNumSent = 2;

    memmove(rcvWindow, &rcvWindow[1], sizeof(Segment)*(WIN_SIZE-1));
    memmove(rcvWinFlag, &rcvWinFlag[1], sizeof(int)*(WIN_SIZE-1));
    memset(&rcvWinFlag[WIN_SIZE-1], 0, sizeof(int)*1);

    /* Sblocco recv_thread per invio ack */
    canSendAck = 1; 

    pthread_mutex_unlock(&consumeLock);

    wprintf(L"\nFiles on server: \n\n");
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

/* Funzione per la gestione del comando download */
int download(char *filename) {

    int *tmpIntBuff;
    char *tmpStrBuff;
    char *tmpStr;
    int countRetransmission = 1;
    int ret = -1;
    int slot;
    int totalSegsRcv = 0;
    int firstArrived = 0;
    pthread_t consumeTid = -1;

    Segment *sndSegment = NULL;
    Segment *rcvSegment = (Segment*)malloc(sizeof(Segment));
    if(rcvSegment == NULL)
    {
        wprintf(ERROR L"While trying to \"malloc\" a new download rcvsegment!\nClosing...\n");
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
        if(ret<0) {
            /* Tenta la ritrasmissione al massimo 7 volte incrementando di volta in volta il timeout (tempo totale massimo 29 secondi) */
            switch(countRetransmission) {
                case 1:
                    requestTimeout.tv_sec = 1;
                    break;

                case 2:
                    requestTimeout.tv_sec = 3;
                    break;

                case 8:
                    requestTimeout.tv_sec = 0;  /* Reset del timeout a 0 (= attendi all'infinito) */

                    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &requestTimeout, sizeof(requestTimeout)) < 0) {
                        wprintf(L"\n" ERROR "While setting request-timeout at try n°: %d\n", countRetransmission);
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
                wprintf(L"\n" ERROR "While setting request-timeout at try n°: %d\n", countRetransmission);
                exit(-1);
            }
        }

        /* Invio Download Request */
        if(randomSendTo(sockfd, sndSegment, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket, loss_prob)){
            if(debug) wprintf(L"\n" DOWNLOAD "Request sent to the server (%s:%d)\n", inet_ntoa(operationServerSocket.sin_addr), ntohs(operationServerSocket.sin_port));
        }
        else{
            if(debug) wprintf(L"\n" DOWNLOAD "Request operation Download lost\n");
        }

        countRetransmission += 1;
    } while((ret = recvSegment(sockfd, rcvSegment, &operationServerSocket, &addrlenOperationServerSocket)) < 0 || atoi(rcvSegment -> synBit) == 1 || atoi(rcvSegment -> cmdType) != 2);

    /* Il file non esiste e ricevo FileNotFound */
    if(atoi(rcvSegment -> eotBit) == 1 && atoi(rcvSegment -> seqNum) == 1) {
        requestTimeout.tv_sec = 0;
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &requestTimeout, sizeof(requestTimeout)) < 0) {
            wprintf(L"\n" ERROR "While setting request-timeout at try n°: %d\n", countRetransmission);
            exit(-1);
        }

        tmpIntBuff = strToInt(EMPTY);
        newSegment(&sndSegment, FALSE, 1, 2, FALSE, TRUE, FALSE, "2", 1, tmpIntBuff);
        free(tmpIntBuff);

        randomSendTo(sockfd, sndSegment, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket, loss_prob);

        free(sndSegment);
        free(rcvSegment);

        wprintf(ERROR L"File not found, please try again!\n");

        return 0;
    }
    
    /* Timeout per la ricezione di un segmento: se rimango 64 secondi in attesa, presumibilmente il
       server si è disconnesso */
    requestTimeout.tv_sec = 64; 
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &requestTimeout, sizeof(requestTimeout)) < 0) {
        wprintf(L"\n" ERROR "While setting request-timeout at try n°: %d\n", countRetransmission);
        exit(-1);
    }

    wprintf(L"\n" INFO "Receiving file...\n");
    wprintf(L"\n\033[1;93mDownload Speed\t Average Speed\t Bytes\033[0m\n");
    wprintf(L"0.00kB/s\t 0.00kB/s\t 0\n\n");

    wprintf(L" %lc", 0x258e);
    for (int i = 0; i < 50; i++) {
        wprintf(L"%lc", 0x2591);
    }
    wprintf(L"%lc\n\n\n\033[3A", 0x2595);

    gettimeofday(&tvDownloadTime, NULL);
    
    while(1) {
        
        lastSeqNumRecv = atoi(rcvSegment -> seqNum);

        if(debug) wprintf(L"\n" DOWNLOAD "Ricevuto pacchetto - (seqNum: %d)\n", atoi(rcvSegment -> seqNum));

        pthread_mutex_lock(&consumeLock);

        /* Calcolo slot finestra e memorizzazione segmento ricevuto e aggiornamento rcvWinFlag */
        slot = normalizeDistance(lastSeqNumRecv, lastAckNumSent);

        /* Primo pacchetto ricevuto dal server contenente filename e dimensione */
        if(firstArrived == 0 && slot == 0) {

            firstArrived = 1;

            tmpStrBuff = intToStr(rcvSegment -> msg, atoi(rcvSegment -> lenMsg));
            tmpStr = strtok(tmpStrBuff, "\b");
            totalSegs = atoi(tmpStr);

            tmpStr = strtok(NULL, "\b");
            strcpy(filename, tmpStr);

            tmpStr = strtok(NULL, "\b");
            totalBytes = atoi(tmpStr);

            tmpStr = strtok(NULL, "\b");
            strcpy(originalFileSHA256, tmpStr);

            free(tmpStrBuff);

            rcvWindow[0] = *rcvSegment;
            rcvWinFlag[0] = 1;

            totalSegsRcv++;

            /* Creazione di un thread per la scrittura dei segmenti su disco */
            if(pthread_create(&consumeTid, NULL, thread_consumeSegment, (void *)filename) != 0)
            {
                wprintf(ERROR L"New consumeSegment thread error\n");
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

        /* Se è consumabile aspetto che il consume scriva su file */
        if(rcvWinFlag[0] == 1) {
            while(canSendAck == 0); /* Attendi che il consume abbia consumato e aggiornato lastAckNumSent */
            canSendAck = 0;
        }

        lastSeqNumSent = lastSeqNumSent%maxSeqNum + 1;

        tmpIntBuff = strToInt(rcvSegment -> seqNum);
        newSegment(&sndSegment, FALSE, lastSeqNumSent, lastAckNumSent, FALSE, TRUE, FALSE, "2", strlen(rcvSegment -> seqNum), tmpIntBuff);
        randomSendTo(sockfd, sndSegment, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket, loss_prob);
        if(debug) wprintf(L"\n" DOWNLOAD "Invio ACK (seqNum: %d) - (ackNum: %d) - (seqNumAcked: %d)\n", atoi(sndSegment -> seqNum), atoi(sndSegment -> ackNum), atoi(rcvSegment -> seqNum));
        free(tmpIntBuff);

        if(totalSegsRcv == totalSegs) {
            pthread_join(consumeTid, NULL);
            break;
        }

        if(recvSegment(sockfd, rcvSegment, &operationServerSocket, &addrlenOperationServerSocket) < 0) {
            wprintf(L"\n" ERROR "Server dead. Download FAILED!\n");

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

/* Thread per la consumazione dei segmenti durante operazione Download */
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

            lastAckNumSent = 2; 

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
            free(tmpStrBuff);

            rcvWinFlag[slot] = 0;

            /* Percentuale di segmenti ricevuti */
            consumedSegments++;
            
            /* Corretta */
            wprintf(L"\033[2A\033[2K\033[100D%.2fkB/s\t %.2fkB/s\t %d/%d\033[2B\033[100D", averageSpeed, bytesRecv/elapsedTime(tvDownloadTime), (consumedSegments*100)/(float)(totalSegs-1), bytesRecv, totalBytes);
            printDownloadStatusBar(bytesRecv, totalBytes, &oldHalfPercentage);
            wprintf(L"\033[100D\033[%dC\033[0K\033[1;93m%.2f%%\033[0m", 54, (consumedSegments*100)/(float)(totalSegs-1));

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

                wprintf(L"\n\n" INFO "Download time: %.2f sec", elapsedTime(tvDownloadTime)/1000);

                wprintf(L"\n" INFO "Calculating SHA256...");

                currentFileSHA256 = getFileSHA256(filename);

                if(strcasecmp(currentFileSHA256, originalFileSHA256) == 0)
                    wprintf(L"\r" INFO "Download completed successfully!\nSHA256: %s\n", currentFileSHA256);
                else
                    wprintf(L"\r" ERROR "There was an error, download gone wrong!\nOriginal SHA256: %s\n Current SHA256: %s\n", originalFileSHA256, currentFileSHA256);

                pthread_exit(0);
            }

            slot++;

        } while(rcvWinFlag[slot] == 1 && slot < WIN_SIZE);        

        /* AckNum da inviare al server */
        lastAckNumSent = atoi(rcvWindow[slot-1].seqNum)%maxSeqNum + 1;

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

/* Funzione per la gestione del comando upload */
int upload(FILE *fileToUpload) {

    Segment *sndSegment = NULL;
    int *tmpIntBuff;
    int countRetransmission = 1;
    int ret;
    int *joinRet;

    fseek(fileToUpload, 0, SEEK_END);
    int fileLen = ftell(fileToUpload);
    fseek(fileToUpload, 0, SEEK_SET);

    /* (A-1)/B+1, parte intera superiore della divisione A/B */
    int totalSegs = (fileLen-1)/(LEN_MSG) + 1;

    Segment *rcvSegment = (Segment*)malloc(sizeof(Segment));
    if(rcvSegment == NULL)
    {
        wprintf(ERROR L"While trying to \"malloc\" a new upload rcvsegment!\nClosing...\n");
        exit(-1);
    } 

    ret = pthread_create(&timeTid, NULL, timeout_thread, NULL);
    if(ret != 0)
    {
        wprintf(ERROR L"New timeout thread error\n");
        exit(-1);
    }
    ret = pthread_create(&sendTid, NULL, continuous_send_thread, NULL);
    if(ret != 0)
    {
        wprintf(ERROR L"New continuous send thread error\n");
        exit(-1);
    }
    ret = pthread_create(&recvTid, NULL, continuous_recv_thread, NULL);
    if(ret != 0)
    {
        wprintf(ERROR L"New continuous recv thread error\n");
        exit(-1);
    }

    int buffFile[LEN_MSG];
    int ch;
    int i, j;
    int currentPos = 1; 

    /* Timer associato al Upload Request */
    struct timeval requestTimeout;
    requestTimeout.tv_usec = 0;
    /* Timeout per la ricezione di un segmento: se rimango 64 secondi in attesa, presumibilmente il
       server si è disconnesso */
    requestTimeout.tv_sec = 64; 
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &requestTimeout, sizeof(requestTimeout)) < 0) {
        wprintf(L"\n" ERROR "While setting request-timeout at try n°: %d\n", countRetransmission);
        exit(-1);
    }

    /* Caricamento del primo pacchetto contenente \bTOTALSEGS\bNOME\bCODICE_SHA256 */
    char tmpStrBuff[LEN_MSG];
    char *fileSHA256 = getFileSHA256(fileToUploadName);

    sprintf(tmpStrBuff, "\b%d\b%s\b%s\b", totalSegs+1, fileToUploadName, fileSHA256);
    tmpIntBuff = strToInt(tmpStrBuff);
    newSegment(&sndSegment, FALSE, 1, -1, FALSE, TRUE, FALSE, "3", strlen(tmpStrBuff), tmpIntBuff);

    sndWindow[sndWinPos] = *sndSegment;
    segToSend[sndWinPos] = 1;
    sndWinPos++;    

    free(tmpIntBuff);

    /* Caricamento segmenti da inviare nella finestra sndWindow */
    for(i=1; i<=totalSegs; i++) {
        
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
            if(recvDead) {
                fclose(fileToUpload);
                fileToUpload = NULL;
                pthread_join(recvTid, NULL);
                return -1;
            }
        }

        pthread_rwlock_rdlock(&slideLock);

        sndWinPos++;
        sndWindow[sndWinPos-1] = *sndSegment;
        segToSend[sndWinPos-1] = 1;

        if(debug) wprintf(L"\n" UPLOAD "Caricato pacchetto: (seqNum: %d) - (sndWinPos: %d)\n", atoi(sndSegment -> seqNum), sndWinPos-1);

        if(pthread_rwlock_unlock(&slideLock) != 0) {
            wprintf(L"%d : %s\n", errno, strerror(errno));
            exit(-1);
        }
    }

    if(debug) wprintf(UPLOAD L"Fine caricamento pacchetti\n");
    fclose(fileToUpload);
    fileToUpload = NULL;

    pthread_join(recvTid, (void**)&joinRet);
    if(*joinRet == -1)
        return -1;

    return 0;
}

/* Thread per la gestione del timeout dei segmenti durante operazione Upload */
void *timeout_thread() {
    
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    int timeoutPos = 0;    
    int maxSeqNumSendable;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    tv.tv_sec += 86400; /* Per impostare un timeout molto elevato, nel "futuro" */

    while(1) {

        checkIfMustDie(TIMEOUT);

        pthread_rwlock_rdlock(&slideLock);

        /* Se il segmento sndWindow[timeoutPos] è stato inviato allora controlla il suo timeout */
        if(segSent[timeoutPos] == 1) {
            /* Se il timeout per il segmento in esame è scaduto */
            if(elapsedTime(segTimeout[timeoutPos]) > 100) {
                
                pthread_mutex_lock(&queueLock);

                segTimeout[timeoutPos] = tv;

                if(debug) wprintf(L"\n" TIME "Timeout scaduto: (seqNum: %d) - (timeoutPos: %d)\n", atoi(sndWindow[timeoutPos].seqNum), timeoutPos);

                maxSeqNumSendable = (atoi(sndWindow[0].seqNum) + WIN_SIZE-2)%(maxSeqNum) + 1;
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

/* Thread per la gestione dell'invio dei segmenti durante operazione Upload */
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

            if(randomSendTo(sockfd, &(queueHead -> segment), (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket, loss_prob) == 1) {
                if(debug) wprintf(L"\n" UPLOAD "[RAND_SENDTO-QUEUE] -> pacchetto inviato seqNum: %d\n", atoi((queueHead -> segment).seqNum));
            }
            else {
                if(debug) wprintf(L"\n" UPLOAD "[RAND_SENDTO-QUEUE] -> pacchetto perso seqNum: %d\n", atoi((queueHead -> segment).seqNum));
            }

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
            if(debug) wprintf(L"\n" UPLOAD "[RAND_SENDTO] -> pacchetto inviato seqNum: %d - (sndPos: %d) - (cmdType: %s)\n", atoi(sndWindow[sndPos].seqNum), sndPos, sndWindow[sndPos].cmdType);
        }
        else {
            if(debug) wprintf(L"\n" UPLOAD "[RAND_SENDTO] -> pacchetto perso seqNum: %d - (sndPos: %d)\n", atoi(sndWindow[sndPos].seqNum), sndPos);
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

/* Thread per la ricezione degli ACK durante operazione Upload */
void *continuous_recv_thread() {
    
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    
    RTT_Data *rttData = initData();

    Segment *rcvSegment = (Segment*)malloc(sizeof(Segment));
    if(rcvSegment == NULL)
    {
        wprintf(ERROR L"While trying to \"malloc\" a new rcvSegment!\nClosing...\n");
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
        wprintf(L"\n" ERROR "While setting repeatTimeout-timeout\n");
        exit(-1);
    }

    gettimeofday(&deadLineTimeout, NULL);
    
    while(1) {

        if(debug==0) wprintf(L"\r" INFO "Upload in progress...");

        if(recvSegment(sockfd, rcvSegment, &operationServerSocket, &addrlenOperationServerSocket) < 0) {
            if(elapsedTime(deadLineTimeout) > 60*1000) {
                wprintf(ERROR L"Server dead. Upload FAILED!\n");
                recvDead = 1;
                pthread_exit(NULL);
            }
            else
                continue;
        }
        gettimeofday(&deadLineTimeout, NULL);

        tmpStrBuff = intToStr(rcvSegment -> msg, atoi(rcvSegment -> lenMsg));
        rcvAck = atoi(rcvSegment -> ackNum);
        rcvAckedSeq = atoi(tmpStrBuff);
        if(debug) wprintf(L"\n" UPLOAD "Ricevuto ACK: (seqNum: %d) - (ackNum: %d) - (seqNumAcked: %d)\n", atoi(rcvSegment -> seqNum), rcvAck, rcvAckedSeq);
        free(tmpStrBuff);

        /* Se l'ack ricevuto ha il eot bit impostato ad 1 allora il server ha ricevuto tutto e
           ha chiesto la chiusura della connessione */
        if(atoi(rcvSegment -> eotBit) == 1) {
            if(debug) wprintf(L"\n" UPLOAD "[RECV] -> Ricevuto pacchetto di FIN: (seqNum: %d) - (ackNum: %d)\n", atoi(rcvSegment -> seqNum), rcvAck);
            mustDie[SEND] = 1;
            mustDie[TIMEOUT] = 1;
            pthread_join(sendTid, NULL);
            pthread_join(timeTid, NULL);

            *joinRet = 0;
            pthread_exit(joinRet);
        }

        /* Se c'è almeno un segmento caricato processa l'ack */
        if(sndWinPos != 0) {

            /* Aggiornamento dell'RTO */
            rttPos = normalizeDistance(rcvAckedSeq, atoi(sndWindow[0].seqNum));
            if(rttPos < WIN_SIZE) {
                if(rttFlag[rttPos] == 0) {
                    RTO = calculateRTO(segRtt[rttPos], rttData);
                    rttFlag[rttPos] = 1;
                }
            }

            /* Calcolo della dimensione dell'eventuale slide da effettuare */
            slideSize = calcSlideSize(rcvAck, atoi(sndWindow[0].seqNum));

            if(rcvAck == atoi(sndWindow[0].seqNum) || slideSize > WIN_SIZE) {
                if(rcvAck == lastAck) {
                    /* Meccanismo del Fast Retransmit: aggiunta del segmento in testa alla coda di ritrasmissione */
                    if(++countAck == 3) {
                        pthread_rwlock_wrlock(&slideLock);
                        if(debug) wprintf(L"\n" UPLOAD "[RECV] -> Fast Retransmit for seqNum: %d\n", rcvAck);
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

                lastAck = rcvAck;
                countAck = 1;

                /* Eliminazione dalla coda di tutti i segmenti più vecchi rispetto all'ack appena ricevuto */
                int maxSeqNumSendable = (atoi(sndWindow[0].seqNum) + WIN_SIZE-2)%(maxSeqNum) + 1;
                int distance = normalizeDistance(maxSeqNumSendable, rcvAck);
                distance = distance >= WIN_SIZE ? WIN_SIZE-1 : distance;
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
    }
}

/* Threada per la gestione della fase di fin */
void *fin_thread() {

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
        wprintf(ERROR L"While trying to \"malloc\" a new handshake segment!\nClosing...\n");
        exit(-1);
    }

    wprintf(L"\n" INFO "Disconnecting from the server... Press CTRL+C to force disconnection\n");

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
                        wprintf(L"\n" ERROR "While setting fin-timeout at try n°: %d\n", countRetransmission);
                        exit(-1);
                    }

                    free(rcvSegment);
                    free(sndSegment);

                    pthread_exit(NULL);

                    break;

                default:
                    finTimeout.tv_sec = 7;
                    break;
            }

            if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &finTimeout, sizeof(finTimeout)) < 0) {
                wprintf(L"\n" ERROR "While setting fin-timeout at try n°: %d\n", countRetransmission);
                exit(-1);
            }

            /* Invio SYN */
            if(randomSendTo(sockfd, sndSegment, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket, loss_prob))
                if(atoi(isAck) == 1) {
                    if(debug) wprintf(L"\n" FINACK "Sent to the server (%s:%d)\n", inet_ntoa(operationServerSocket.sin_addr), ntohs(operationServerSocket.sin_port));
                }
                else{
                    if(debug) wprintf(L"\n" FIN "Sent to the server (%s:%d)\n", inet_ntoa(operationServerSocket.sin_addr), ntohs(operationServerSocket.sin_port));
                }
            else
                if(atoi(isAck) == 1){
                    if(debug) wprintf(L"\n" FINACK "LOST\n");
                }
                else{
                    if(debug) wprintf(L"\n" FIN "LOST\n");
                }

            countRetransmission += 1;
        }

        /* Ricezione FIN-ACK */
    } while((ret=recvSegment(sockfd, rcvSegment, &operationServerSocket, &addrlenOperationServerSocket)) < 0 || atoi(rcvSegment -> ackBit) != 1);

    finTimeout.tv_sec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &finTimeout, sizeof(finTimeout)) < 0) {
        wprintf(L"\n" ERROR "While setting fin-timeout at try n°: %d\n", countRetransmission);
        exit(-1);
    }

    if(atoi(rcvSegment -> finBit) != 1) {
        if(debug) wprintf(L"\n" FIN "ACK del FIN ricevuto dal server\n");

        finTimeout.tv_sec = 30; 
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &finTimeout, sizeof(finTimeout)) < 0) {
            wprintf(L"\n" ERROR "While setting fin-timeout at try n°: %d\n", countRetransmission);
            exit(-1);
        }
        finTimeout.tv_sec = 0;

        /* Ricezione FIN del Server */
        if(recvSegment(sockfd, rcvSegment, &operationServerSocket, &addrlenOperationServerSocket) < 0) {
            if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &finTimeout, sizeof(finTimeout)) < 0) {
                wprintf(L"\n" ERROR "While setting fin-timeout\n");
                exit(-1);
            }

            wprintf(L"\n" ERROR "Server unreachable, disconnected...\n");

            signal(SIGINT, SIG_DFL);

            free(sndSegment);
            free(rcvSegment);

            pthread_exit(NULL);
        }
        
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &finTimeout, sizeof(finTimeout)) < 0) {
            wprintf(L"\n" ERROR "While setting fin-timeout\n");
            exit(-1);
        }

        if(debug) wprintf(L"\n" FIN "FIN del server ricevuto\n");
    }

    /* Invio ACK del FIN del Server */
    lastAckNumSent = atoi(rcvSegment -> seqNum)%maxSeqNum + 1;
    tmpIntBuff = strToInt(EMPTY);
    newSegment(&sndSegment, FALSE, 2, lastAckNumSent, FALSE, TRUE, FALSE, EMPTY, 1, tmpIntBuff);
    free(tmpIntBuff);
    randomSendTo(sockfd, sndSegment, (struct sockaddr*)&operationServerSocket, addrlenOperationServerSocket, loss_prob);
    if(debug) wprintf(FIN L"ACK del FIN inviato al server\n");

    wprintf(L"\n" INFO "Successfully disconnected from Server\n");

    signal(SIGINT, SIG_DFL);

    free(sndSegment);
    free(rcvSegment);

    pthread_exit(NULL);
}

/* Funzione per il ripristino delle variabili globali tra un comando e l'altro */
void resetData() {

    if (wrFile != NULL){
        fclose(wrFile);
        wrFile = NULL;
    }

    lastSeqNumSent = 0;
    lastAckNumSent = 1;
    totalSegs = WIN_SIZE;
    canSendAck = 0;
    sendQueue = 1;
    sndWinPos = 0;
    sndPos = 0;
    recvDead = 0;
    queueHead = NULL;

    memset(sndWindow, '\0', sizeof(Segment)*WIN_SIZE);
    memset(rcvWindow, '\0', sizeof(Segment)*WIN_SIZE);
    memset(rcvWinFlag, '\0', sizeof(int)*WIN_SIZE);
    memset(segToSend, '\0', sizeof(int)*WIN_SIZE);
    memset(segSent, '\0', sizeof(int)*WIN_SIZE);
    memset(rttFlag, '\0', sizeof(int)*WIN_SIZE);
    memset(segTimeout, '\0', sizeof(struct timeval)*WIN_SIZE);
    memset(segRtt, '\0', sizeof(struct timeval)*WIN_SIZE);
    memset(mustDie, '\0', sizeof(int)*4);
}

/* Funzione che controlla la terminazione dei thread */
void checkIfMustDie(int threadNum) {
    if(mustDie[threadNum])
        pthread_exit(NULL);
}