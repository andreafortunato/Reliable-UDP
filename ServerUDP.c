// https://packetlife.net/blog/2010/jun/7/understanding-tcp-sequence-acknowledgment-numbers/

// Server - FTP on UDP 

//////////////////////////////////////////////////////////////////////////
// sha256sum -z cuore.png | cut -d " " -f 1 --> d92d0c0bd9977bdfe8941302ddc6ab940aa2958e6bc9156937d5f25c8a6e781c
//////////////////////////////////////////////////////////////////////////

#include <time.h>
#include <signal.h>
#include <stdbool.h>
#include "Server.h"

#define IP_PROTOCOL 0

/* Prototipi */
void *client_thread_handshake(void *);      /* Thread associato al client */
void *client_thread_list(void *);
void *client_thread_download(void *);
void *client_thread_upload(void *);
void list(ClientNode *client);
void download(ClientNode *client, char *fileName);
void fin(ClientNode *client);
void ctrl_c_handler();

/**********/

void *timeout_thread(void *);
void *continuous_send_thread(void *);
void *continuous_recv_thread(void *);

/**********/


/* Variabili globali */
pthread_mutex_t lockList;                  /* Semaforo Read/Write necessario per la gestione degli accessi alla lista */
int syncFlag;
int debug = 0;                              /* Se l'utente ha avviato in modalità debug, verranno mostrate informazioni aggiuntive */

char **fileNameList;
int numFiles = 0;

ClientNode *clientList = NULL;
int clientListSize = 0;
int maxSockFd = 0;

int maxSeqNum;

// Variabili da buttare dentro i singoli thread relativi ad ogni operazione
                                        // Main		Timeout		Send	Recv
Segment sndWindow[WIN_SIZE];            // W					R		R/W
int segToSend[WIN_SIZE];                // W					R/W
int segSent[WIN_SIZE];                  // 			R			W		W
int sndWinPos = 0;                      // R/W							W
int sndPos = 0;                         // 						R/W		W		(Mutex: sndPosLock)
struct timeval segTimeout[WIN_SIZE];    // 			R			W				(Nulla, non ci interessa la concorrenza)
struct timeval segRtt[WIN_SIZE];        // 
double RTO = 5000;
SegQueue *queueHead = NULL;             // 			W			R/W				(Mutex: queueLock)
Segment rcvWindow[WIN_SIZE];            // 
int rcvWinFlag[WIN_SIZE];               // 
int rttFlag[WIN_SIZE];
pthread_mutex_t queueLock;				// 			X			X
pthread_rwlock_t slideLock;             // 
pthread_t timeoutTid, sendTid;
int sendQueue;
ClientNode *clientThread;
int addrlenClientThread;
Sockaddr_in clientSocketThread;

// Main - Server 
int main(int argc, char *argv[])
{
    /* Inizializzazione sessione server */
    setbuf(stdout,NULL);
    srand(time(0));
    system("clear");

    /* Gestione del SIGINT (Ctrl+C) */
    //signal(SIGINT, ctrl_c_handler);

    /* Inizializzazione semaforo R/W */
    if(pthread_mutex_init(&lockList, NULL) != 0) {
        printf("Failed semaphore initialization.\n");
        exit(-1);
    }
    
    /* Parse dei parametri passati da riga di comando */
    /************************************************************
    int port = parseCmdLine(argc, argv, "server", NULL, &debug);
    if(port == -1)
    {
        exit(0);
    }
    ************************************************************/

    maxSeqNum = WIN_SIZE*2;

    fileNameList = getFileNameList(argv[0]+2, &numFiles);

    /* Dichiarazione variabili locali Main */
    int mainSockFd;     /*  */
    int ret;            /*  */

    pthread_t tid;      /*  */

    ClientNode *checkClient; 

    /* Sockaddr_in server */
    Sockaddr_in serverSocket;
    bzero(&serverSocket, sizeof(Sockaddr_in));
    serverSocket.sin_family = AF_INET;
    serverSocket.sin_addr.s_addr = htonl(INADDR_ANY);
    //serverSocket.sin_port = htons(port);
    serverSocket.sin_port = htons(47435);
    int addrlenServer = sizeof(serverSocket);

    /* Sockaddr_in client */
    Sockaddr_in clientSocket;
    bzero(&clientSocket, sizeof(Sockaddr_in));
    int addrlenClient = sizeof(clientSocket);

    /* Struct Segment di ricezione */
    Segment *rcvSegment = (Segment*)malloc(sizeof(Segment));
	if(rcvSegment == NULL)
	{
		printf("Error while trying to \"malloc\" a new rcvSegment!\nClosing...\n");
		exit(-1);
	}

	/* Struttura threadArgs per parametri del thread */
    ThreadArgs *threadArgs = (ThreadArgs*)malloc(sizeof(ThreadArgs));
	if(threadArgs == NULL)
	{
		printf("Error while trying to \"malloc\" a new ThreadArgs!\nClosing...\n");
		exit(-1);
	}

    /* Socket - UDP */
    mainSockFd = socket(AF_INET, SOCK_DGRAM, IP_PROTOCOL);
    if (mainSockFd < 0){
        printf("\nFile descriptor not received!\n");
        exit(-1);
    }

    /* Assegnazione dell'indirizzo locale alla socket */
    if (bind(mainSockFd, (struct sockaddr*)&serverSocket, addrlenServer) == 0) {
        printf("\nServer online... - PID: %d\n\n", getpid());
    }
    else {
        printf("\nBinding Failed!\n");
        exit(-1);
    }

    /* Server in attesa di richieste da parte dei client */
    while(1) {

        bzero(rcvSegment, sizeof(Segment));
        bzero(threadArgs, sizeof(ThreadArgs));

        /* Controllo che il segmento ricevuto sia di syn, in caso negativo ignoro il segmento e ne attendo un altro */
        recvSegment(mainSockFd, rcvSegment, &clientSocket, &addrlenClient);
        if(atoi(rcvSegment -> synBit) != 1) {
            continue;
        }

        /* Controllo se il client ha già effettuato una richiesta di SYN: in caso affermativo,
           cancello l'handshake_thread precedentemente associato e lo ricreo */
        pthread_mutex_lock(&lockList);
        checkClient = clientList;
        while(checkClient != NULL) {
            if(strcmp(checkClient->ip, inet_ntoa(clientSocket.sin_addr)) == 0 || checkClient->clientPort == ntohs(clientSocket.sin_port))
                break;
            else
                checkClient = checkClient -> next;
        }
        if (checkClient != NULL) {
            printf("Il client (%s:%d) era già connesso, lo termino\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));
            pthread_cancel(checkClient -> tid);
            pthread_join(checkClient -> tid, NULL);

            close(checkClient -> sockfd);
            deleteClientNode(&clientList, checkClient, &clientListSize, &maxSockFd);
        }
        pthread_mutex_unlock(&lockList);

        printf("\n[PKT-MAIN]: Client (%s:%d) is trying to connect\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));
        
        /* Creazione di un thread utile alla fase di handshake */
        syncFlag = 0;
        threadArgs = newThreadArgs(clientSocket, *rcvSegment);
        ret = pthread_create(&tid, NULL, client_thread_handshake, (void *)threadArgs);
        if(ret != 0)
        {
            printf("New client thread error\n");
            exit(-1);
        }
        
        /* Attesa dell'aggiunta del nuovo client */
        while(syncFlag == 0);
    } 

    /* Distruzione semaforo R/W */
    if(pthread_mutex_destroy(&lockList) != 0) {
        printf("Failed semaphore destruction.\n");
        exit(-1);
    }

    return 0; 
}

void ctrl_c_handler()
{
    printf("\n\n---> STAMPA LISTA FILE <---\n\n");
    printf("Number of files: %d\n", numFiles);
    printf("%s", fileNameListToString(fileNameList, numFiles));
    
    printf("\n\n---> STAMPA LISTA <---\n\n");
    printf("|Size: %d - Max: %d|\n", clientListSize, maxSockFd);
    printList(clientList);

    // int sock;
    // printf("\nSocket da eliminare: ");
    // scanf("%d", &sock);

    // ClientNode *tmp = clientList;

    // while(tmp != NULL)
    // {
    //     if(tmp -> sockfd == sock)
    //         deleteClientNode(&clientList, tmp, &clientListSize, &maxSockFd);

    //     tmp = tmp -> next;
    // }

    // printf("\n\n---> STAMPA LISTA <---\n\n");
    // printf("|Size: %d - Max: %d|\n", clientListSize, maxSockFd);
    // printList(clientList);
}

/* Thread handshake */
void *client_thread_handshake(void *args)
{		
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    int clientSockFd;

    int *tmpIntBuff;
    char *tmpStrBuff;

    ThreadArgs threadArgs = *((ThreadArgs*)args);
    //printf("\n[TEST]: threadArgs for new client (%s:%d)!\n", inet_ntoa(threadArgs -> clientSocket.sin_addr), ntohs(threadArgs -> clientSocket.sin_port));

    /* Sblocca il main per una nuova ricezione */
    syncFlag = 1;

    /* Sockaddr_in client */
    Sockaddr_in clientSocket;
    clientSocket = threadArgs.clientSocket;
    int addrlenClient = sizeof(clientSocket);

    printf("\n[TEST - %ld]: threadArgs for client handshake (%s:%d)!\n", pthread_self(), inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));

    /* Sockaddr_in server */
    Sockaddr_in serverSocket;
    serverSocket.sin_family = AF_INET;
    serverSocket.sin_addr.s_addr = inet_addr(inet_ntoa(clientSocket.sin_addr));
    int addrlenServer = sizeof(serverSocket);

    /* Socket UDP */
    clientSockFd = socket(AF_INET, SOCK_DGRAM, IP_PROTOCOL);
    if (clientSockFd < 0){
        printf("\nFailed creating socket for new client (%s:%d)!\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));
        exit(-1);
    }

    /* Generazione di una porta random per lo scambio dati con il client */
    do {
        serverSocket.sin_port = htons(49152 + rand() % (65535+1 - 49152));
    } while(bind(clientSockFd, (struct sockaddr*)&serverSocket, addrlenServer) != 0);
    
    /* Aggiunta alla lista dei client sul server del client appena connesso */
    ClientNode *newClient = newNode(clientSockFd, inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port), clientSocket, pthread_self(), ntohs(serverSocket.sin_port), (threadArgs.segment).seqNum); 
    pthread_mutex_lock(&lockList);
    addClientNode(&clientList, newClient, &clientListSize, &maxSockFd);
    /*
    printf("New Client\n");
        if(newClient -> prev)
            printf("Prev: %d\n", newClient -> prev->clientPort);
        else
            printf("Prev: NULL\n");

        printf("Sockfd: %d\nIP: %s\nPort: %d\n", newClient -> sockfd, newClient -> ip, newClient -> clientPort);
        
        if(newClient -> next)
            printf("Next: %d\n", newClient -> next->clientPort);
        else
            printf("Next: NULL\n");
    */
    pthread_mutex_unlock(&lockList);

    /* Generazione segmento SYN-ACK */
    tmpIntBuff = strToInt(EMPTY);
    Segment *synAck = NULL;
    newSegment(&synAck, FALSE, 1, atoi((threadArgs.segment).seqNum) + 1, TRUE, TRUE, FALSE, EMPTY, 1, tmpIntBuff);
    free(tmpIntBuff);

    Segment *rcvSegment = (Segment*) malloc(sizeof(Segment));
    if(rcvSegment == NULL)
    {
        printf("Error while trying to \"malloc\" a new ACK segment of SYN-ACK!\nClosing...\n");
        exit(-1);
    }

    /* Timer associato al SYN-ACK */
    struct timeval synTimeout;
    synTimeout.tv_usec = 100*1000;
    synTimeout.tv_sec = 0;

    if (setsockopt(clientSockFd, SOL_SOCKET, SO_RCVTIMEO, &synTimeout, sizeof(synTimeout)) < 0) {
            printf("\nError while setting syn-ack-timeout");
            exit(-1);
    }

    gettimeofday(&synTimeout, NULL);

    sendto(clientSockFd, synAck, sizeof(Segment), 0, (struct sockaddr*)&clientSocket, addrlenClient);
    printf("SYN-ACK sent to the client (%s:%d)\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));
    // /* Invio SYN-ACK */
    // if(randomSendTo(clientSockFd, synAck, (struct sockaddr*)&clientSocket, addrlenClient))
    //     printf("SYN-ACK sent to the client (%s:%d)\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));
    // else
    //     printf("SYN-ACK LOST\n");
    free(synAck);

    /* Attesa ricezione dell'ack del syn-ack per un massimo di 30 secondi, dopodichè
       chiusura della connessione verso quel client */
    while(1) {
        if(elapsedTime(synTimeout) > 30*1000) {
            free(rcvSegment);
            printf("[SYN] Client %s:%d is dead.\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));

            close(newClient -> sockfd);

            pthread_mutex_lock(&lockList);
            deleteClientNode(&clientList, newClient, &clientListSize, &maxSockFd);
            pthread_mutex_unlock(&lockList);

            pthread_exit(NULL);
        }

        /* Controllo che il segmento ricevuto sia un ACK (oppure ACK+operazione), in caso
           negativo ignoro il segmento e ne attendo un altro */
        if(recvSegment(clientSockFd, rcvSegment, &clientSocket, &addrlenClient) < 0 || atoi(rcvSegment -> ackBit) != 1) 
            continue;

        printf("\n[SYN] -> Ricevuta richiesta operazione: %d\n", atoi(rcvSegment -> cmdType));

        break;       
    }

    printf("WHILE SUPERATO\n");

    /* Imposto un timeout di 30 secondi per la ricezione della richiesta di operazione da parte del client */
    synTimeout.tv_usec = 0;
    synTimeout.tv_sec = 30;
    if (setsockopt(clientSockFd, SOL_SOCKET, SO_RCVTIMEO, &synTimeout, sizeof(synTimeout)) < 0) {
            printf("\nError while setting syn-ack-timeout");
            exit(-1);
    }

    /* Se il pacchetto ricevuto è l'ACK del SYN-ACK, attendo il prossimo per la richiesta di operazione */
    if(strcmp(rcvSegment -> cmdType, EMPTY) == 0) {
        if(recvSegment(clientSockFd, rcvSegment, &clientSocket, &addrlenClient) < 0) { /* Ricezione operazione */
            synTimeout.tv_usec = 0;
            synTimeout.tv_sec = 0;
            if (setsockopt(clientSockFd, SOL_SOCKET, SO_RCVTIMEO, &synTimeout, sizeof(synTimeout)) < 0) {
                printf("\nError while setting syn-ack-timeout");
                exit(-1);
            }

            close(newClient -> sockfd);

            pthread_mutex_lock(&lockList);
            deleteClientNode(&clientList, newClient, &clientListSize, &maxSockFd);
            pthread_mutex_unlock(&lockList);

            free(rcvSegment);

            pthread_exit(NULL);
        }
    }


    /* Reset del timeout per la recvfrom */
    synTimeout.tv_usec = 0;
    synTimeout.tv_sec = 0;
    if (setsockopt(clientSockFd, SOL_SOCKET, SO_RCVTIMEO, &synTimeout, sizeof(synTimeout)) < 0) {
        printf("\nError while setting syn-ack-timeout");
        exit(-1);
    }

    /* Switch operazioni richieste dal client */
    switch(atoi(rcvSegment -> cmdType)) {

        case 1:
            list(newClient);
            break;

        case 2:
            tmpStrBuff = intToStr(rcvSegment -> msg, atoi(rcvSegment -> lenMsg));
            printf("FILE RICHIESTO (%s)\n", tmpStrBuff);
            download(newClient, tmpStrBuff);
            free(tmpStrBuff);
            break;

        case 3:
            break;

        default:

            break;

    }

    free(rcvSegment);

    pthread_exit(NULL);
}

void list(ClientNode *client) {

    int addrlenClient = sizeof(client -> connection);
    int totalSegs;
    int i,j;
    int lenBuff;
    int currentPos = 1;
    char *tmpStrBuff;
    char *filelist;
    int tmpIntBuff[LEN_MSG];

    Segment *sndSegment = NULL;

    Segment *rcvSegment = (Segment*) malloc(sizeof(Segment));
    if(rcvSegment == NULL)
    {
        printf("Error while trying to \"malloc\" a new segment for list operation!\nClosing...\n");
        exit(-1);
    }

    /* Invio della lista dei file al Client */
    filelist = fileNameListToString(fileNameList, numFiles);
    lenBuff = strlen(filelist);
    
    totalSegs = (lenBuff-1)/(LEN_MSG) + 1; /* Formula della vita: (A-1)/B+1, parte intera superiore della divisione A/B */

    for(i=0; i<totalSegs; i++) {
        bzero(tmpIntBuff, sizeof(int)*LEN_MSG);

        /* */
        for(j = 1; (j <= LEN_MSG) && (currentPos < lenBuff); j++, currentPos++) {
            tmpIntBuff[j-1] = htonl((int)filelist[currentPos-1]);
        }

        if(i == totalSegs-1)
            newSegment(&sndSegment, TRUE, (i%maxSeqNum)+1, -1, FALSE, FALSE, FALSE, "1", j-1, tmpIntBuff);
        else
            newSegment(&sndSegment, FALSE, (i%maxSeqNum)+1, -1, FALSE, FALSE, FALSE, "1", j-1, tmpIntBuff);

        sendto(client -> sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&(client -> connection), addrlenClient);
        tmpStrBuff = intToStr(tmpIntBuff, j-1);
        //printf("\nInvio pacchetto - (seqNum: %d) - (msg: %s)\n", atoi(sndSegment -> seqNum), tmpStrBuff);
        bzero(tmpStrBuff, strlen(tmpStrBuff));
        recvSegment(client -> sockfd, rcvSegment, &(client -> connection), &addrlenClient);
        //printf("Ricevuto pacchetto - (ackNum: %d)\n", atoi(rcvSegment -> ackNum));
    }

    free(tmpStrBuff);
    free(sndSegment);
    free(rcvSegment);

    /* Fase di FIN */
    fin(client);
}

/* Funzione download */
void download(ClientNode *client, char *fileName)
{   
    char *originalFileName = fileExist(fileNameList, fileName, numFiles);
    printf("FILE SUL SERVER: %s\n", originalFileName);

    /* Se il file è presente nel server */
    if(originalFileName != NULL) {

        int ret;
    	pthread_t uselessTid;
    	Segment *sndSegment = NULL;

        /* Apertura del file richiesto dal client */
        FILE *file = fopen(originalFileName, "rb");

        // char b[1024];
        // getcwd(b, 1024);
        // printf("%s", b);

        fseek(file, 0, SEEK_END);
        int fileLen = ftell(file);
        fseek(file, 0, SEEK_SET);

        /* Formula della vita: (A-1)/B+1, parte intera superiore della divisione A/B */
        int totalSegs = (fileLen-1)/(LEN_MSG) + 1; 

        /* Inizializzazione strutture condivise */
        for(int i=0; i<WIN_SIZE; i++) {
            segToSend[i] = 0;
            segSent[i] = 0;
            rttFlag[i] = 0;
        }

        /* Inizializzazione semaforo R/W */
        if(pthread_rwlock_init(&slideLock, NULL) != 0) {
            printf("Failed slideLock semaphore initialization.\n");
            exit(-1);
        }

        if(pthread_mutex_init(&queueLock, NULL) != 0) {
        	printf("Failed queueLock semaphore initialization.\n");
            exit(-1);
        }

        /* Abilita lettura della coda per il thread send */
        sendQueue = 1;

        clientThread = client;
        addrlenClientThread = sizeof(clientThread -> connection);

        ret = pthread_create(&timeoutTid, NULL, timeout_thread, NULL);
        if(ret != 0)
        {
            printf("New timeout thread error\n");
            exit(-1);
        }
        ret = pthread_create(&sendTid, NULL, continuous_send_thread, NULL);
        if(ret != 0)
        {
            printf("New continuous send thread error\n");
            exit(-1);
        }
        ret = pthread_create(&uselessTid, NULL, continuous_recv_thread, NULL);
        if(ret != 0)
        {
            printf("New continuous recv thread error\n");
            exit(-1);
        }

        int buffFile[LEN_MSG];
        int ch;
        int i, j;
        int currentPos = 1; 

        /* Caricamento del primo pacchetto contenente \bTOTALSEGS\bNOME\b */  
        char tmpStrBuff[LEN_MSG];
        int *tmpIntBuff;
        char *fileSHA256 = getFileSHA256(originalFileName);


        sprintf(tmpStrBuff, "\b%d\b%s\b%d\b%s\b", totalSegs+1, originalFileName, fileLen, fileSHA256);
        tmpIntBuff = strToInt(tmpStrBuff);
        newSegment(&sndSegment, FALSE, 1, -1, FALSE, FALSE, FALSE, "2", strlen(tmpStrBuff), tmpIntBuff);

        sndWindow[sndWinPos] = *sndSegment;
        segToSend[sndWinPos] = 1;
        sndWinPos++;

        free(tmpIntBuff);

        /* Caricamento segmenti da inviare nella finestra sndWindow */
        for(i=1; i<=totalSegs; i++) {
            
            /* Lettura di LEN_MSG caratteri convertiti in formato network e memorizzati in buffFile (Endianness problem) */
            bzero(buffFile, LEN_MSG);
            for(j = 1; j <= LEN_MSG; j++, currentPos++) {
                if((ch = fgetc(file)) == EOF)
                    break;
                buffFile[j-1] = htonl(ch);
                //buffFile[j-1] = htonl(ch)*2; // Per controllare che l'SHA256 calcolato dal client sia diverso
            }

            /* Se è l'ultimo segmento imposta End-Of-Transmission-Bit a 1 */
            if(i == totalSegs)
                newSegment(&sndSegment, TRUE, (i%maxSeqNum)+1, -1, FALSE, FALSE, FALSE, "2", j-1, buffFile);
            else
                newSegment(&sndSegment, FALSE, (i%maxSeqNum)+1, -1, FALSE, FALSE, FALSE, "2", j-1, buffFile);

            /* Attendi slide finestra per il caricamento di altri segmenti */
            while(sndWinPos >= WIN_SIZE);

            pthread_rwlock_rdlock(&slideLock);

            sndWinPos++;

            sndWindow[sndWinPos-1] = *sndSegment;
            segToSend[sndWinPos-1] = 1;

            //printf("\n[MAIN] -> Caricato pacchetto: (seqNum: %d) - (sndWinPos: %d)\n", atoi(sndSegment -> seqNum), sndWinPos-1);

            pthread_rwlock_unlock(&slideLock);
        }

        //printf("Fine caricamento pacchetti\n");
        fclose(file);
    }
    else {
        /* Se il file non è presente sul server notifico il client */
        printf("\nFile not found!\n");
        // mandare pacchetto con messaggio FILE NOT FOUND!
    }

    pthread_exit(0);
}

void *timeout_thread(void *args) {
	
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    int timeoutPos = 0;    
    int maxSeqNumSendable;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    tv.tv_sec += 86400; // Per impostare un timeout molto elevato, nel "futuro"

    while(1) {
        pthread_rwlock_rdlock(&slideLock);

        /* Se il segmento sndWindow[timeoutPos] è stato inviato allora controlla il suo timeout */
        if(segSent[timeoutPos] == 1) {
            /* Se il timeout per il segmento in esame è scaduto */
            if(elapsedTime(segTimeout[timeoutPos]) > 100) {
                
                pthread_mutex_lock(&queueLock);

                /*************************************************************************************************************************/
                //printf("\n[TIMEOUT] -> Timeout scaduto: (seqNum: %d) - (timeoutPos: %d)\n", atoi(sndWindow[timeoutPos].seqNum), timeoutPos);
                /**************************************************************************************************************************/

                segTimeout[timeoutPos] = tv;

                maxSeqNumSendable = (atoi(sndWindow[0].seqNum) + WIN_SIZE-2)%(maxSeqNum) + 1;
                //printf("SNDWINDOW[0]: %s - maxSeqNumSendable: %d - ", sndWindow[0].seqNum, maxSeqNumSendable);
                orderedInsertSegToQueue(&queueHead, sndWindow[timeoutPos], timeoutPos, maxSeqNumSendable);

                pthread_mutex_unlock(&queueLock);  
            }
        }

        timeoutPos = (timeoutPos+1) % WIN_SIZE;

        pthread_rwlock_unlock(&slideLock);
    }
}

void *continuous_send_thread(void *args) {

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	
    //int sndWinPos = (int*)args;

    SegQueue *prev;

    while(1) {

        /* Attesa sblocco coda di trasmissione da parte del thread recv */
        while(sendQueue == 0);
        pthread_rwlock_rdlock(&slideLock);
        
        /* Invio dell'intera coda */
        while(queueHead != NULL && sendQueue == 1) {

            pthread_mutex_lock(&queueLock);

            //printf("\n[SEND] -> Invio pacchetto in coda: (seqNum: %d) - (winPos: %d)\n", atoi((queueHead -> segment).seqNum), queueHead -> winPos);
            /*************************************************************************************************************************/
            //printf("\nsegTimeout[%d]: %ld\n", queueHead -> winPos, segTimeout[queueHead -> winPos].tv_sec);
            /**************************************************************************************************************************/
            //randomSendTo(clientThread -> sockfd, &(queueHead -> segment), (struct sockaddr*)&(clientThread -> connection), addrlenClientThread);
            /*************************************************************************************************************************/
            if(randomSendTo(clientThread -> sockfd, &(queueHead -> segment), (struct sockaddr*)&(clientThread -> connection), addrlenClientThread) == 1) {
                //printf("[RAND_SENDTO] -> pacchetto inviato seqNum: "); printf("%d\n", atoi((queueHead -> segment).seqNum));
            }
            else {
                //printf("[RAND_SENDTO] -> pacchetto perso seqNum: "); printf("%d\n", atoi((queueHead -> segment).seqNum));
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

            pthread_mutex_unlock(&queueLock);
        }
       
        /* (Primo controllo) Se la recv ha ricevuto un ACK 'corretto', interrompe l'invio della coda (sendQueue=0)
           e resetta lo stato della send;
           (Secondo controllo) Se ho inviato tutta la finestra (ovvero tutti i segToSend sono uguali a 0) resetto 
           lo stato della send */
        if(sendQueue == 0 || segToSend[sndPos] == 0) {
        	pthread_rwlock_unlock(&slideLock);
            continue;
        }

        /* Impostiamo il timestamp per il calcolo dell'rtt */
        gettimeofday(&segRtt[sndPos], NULL);
        
        if(randomSendTo(clientThread -> sockfd, &(sndWindow[sndPos]), (struct sockaddr*)&(clientThread -> connection), addrlenClientThread) == 1) {
            //printf("[RAND_SENDTO] -> pacchetto inviato seqNum: %d - (sndPos: %d)\n", atoi(sndWindow[sndPos].seqNum), sndPos);
        }
        else {
            //printf("[RAND_SENDTO] -> pacchetto perso seqNum: %d - (sndPos: %d)\n", atoi(sndWindow[sndPos].seqNum), sndPos);
        }
        
        /* Impostiamo il timestamp per il calcolo del timeout */
        gettimeofday(&segTimeout[sndPos], NULL);

        segToSend[sndPos] = 0;
        segSent[sndPos] = 1;

        sndPos = (sndPos+1) % WIN_SIZE;

        pthread_rwlock_unlock(&slideLock);
    }
}

void *continuous_recv_thread(void *args){
	
    RTT_Data *rttData = initData();

    Segment *rcvSegment = (Segment*)malloc(sizeof(Segment));
    if(rcvSegment == NULL)
    {
        printf("Error while trying to \"malloc\" a new rcvSegment!\nClosing...\n");
        exit(-1);
    }

    int lastAck = 1;
    int countAck = 0;
    bool invalidAck;
    int rcvAck;
    int rcvAckedSeq;
    int slideSize;
    int rttPos;
    int tmpPos;
    char *tmpStrBuff;
    SegQueue *head;

    while(1) {

        recvSegment(clientThread -> sockfd, rcvSegment, &(clientThread -> connection), &addrlenClientThread);
        tmpStrBuff = intToStr(rcvSegment -> msg, atoi(rcvSegment -> lenMsg));
        rcvAck = atoi(rcvSegment -> ackNum);
        rcvAckedSeq = atoi(tmpStrBuff);
        //printf("\n[RECV] -> Ricevuto ACK: (seqNum: %d) - (ackNum: %d) - (seqNumAcked: %d)", atoi(rcvSegment -> seqNum), rcvAck, rcvAckedSeq);
        free(tmpStrBuff);

        /* Se l'ack ricevuto ha il fin bit impostato ad 1 allora il client ha ricevuto tutto e
           ha chiesto la chiusura della connessione */
        if(atoi(rcvSegment -> finBit) == 1) {
            //printf("\n[RECV] -> Ricevuto pacchetto di FIN: (seqNum: %d) - (ackNum: %d)\n", atoi(rcvSegment -> seqNum), rcvAck);
            pthread_cancel(timeoutTid);
            pthread_cancel(sendTid);
            pthread_join(timeoutTid, NULL);
            pthread_join(sendTid, NULL);

            /* Distruzione semafori */
            if(pthread_rwlock_destroy(&slideLock) != 0) {
                printf("Failed slideLock semaphore destruction.\n");
                exit(-1);
            }

            if(pthread_mutex_destroy(&queueLock) != 0) {
                printf("Failed queueLock semaphore destruction.\n");
                exit(-1);
            }

            fin(clientThread);
            // Free della struttura dati thread
            pthread_exit(0);
        }

        /* Se c'è almeno un segmento caricato processa l'ack */
        if(sndWinPos != 0) {

            /* Aggiornamento dell'RTO */
            rttPos = normalizeDistance(rcvAckedSeq, atoi(sndWindow[0].seqNum));
            if(rttPos < WIN_SIZE) {
                //printf("\nRTT_FLAG[%d] = %d - %d\n", rttPos, rttFlag[rttPos], atoi(sndWindow[0].seqNum));
                if(rttFlag[rttPos] == 0) {
                    RTO = calculateRTO(segRtt[rttPos], rttData);
                    rttFlag[rttPos] = 1;
                }
            }

            /* Calcola se l'ACK ricevuto è inerente ad un pacchetto già riscontrato 
               e/o rientra tra uno dei seqNum appartententi a 
               "sndWindow[0].seqNum, sndWindow[-1].seqNum, ..., sndWindow[-(WIN_SIZE-1)].seqNum" */
            invalidAck = false;
            for(int i = 0; i < WIN_SIZE; i++) {
                invalidAck = invalidAck || (rcvAck == normalize(atoi(sndWindow[0].seqNum), i));
                if(invalidAck)
                    break;
            }

            if(invalidAck) {
                //printf(" - Ack non valido\n");
                if(rcvAck == lastAck) {
                    /* Meccanismo del Fast Retransmit: aggiunta del segmento in testa alla coda di ritrasmissione */
                    if(++countAck == 3) {
                        countAck = 0;
                        if(queueHead != NULL && atoi((queueHead -> segment).seqNum) != rcvAck) {
                            sendQueue = 0; // Stoppiamo il send durante la trasmissione della coda
                            pthread_rwlock_wrlock(&slideLock);
                            head = queueHead;
                            tmpPos = 0;
                            while(atoi(sndWindow[tmpPos++].seqNum) != rcvAck);
                            queueHead = newSegQueue(sndWindow[tmpPos-1], tmpPos-1);
                            queueHead -> next = head;
                            sendQueue = 1; // Riavviamo il send durante la trasmissione della coda
                            pthread_rwlock_unlock(&slideLock);
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
                //printf(" - Ack valido\n");

                /* Calcolo della dimensione dello slide da effettuare */
                slideSize = normalize(rcvAck, atoi(sndWindow[0].seqNum));
                lastAck = rcvAck;
                countAck = 1;

                /* Eliminazione dalla coda di tutti i segmenti più vecchi rispetto all'ack appena ricevuto */
                int maxSeqNumSendable = (atoi(sndWindow[0].seqNum) + WIN_SIZE-2)%(maxSeqNum) + 1;
                int distance = normalizeDistance(maxSeqNumSendable, rcvAck);
                distance = distance >= WIN_SIZE ? WIN_SIZE-1 : distance;
                //printf("Slide: %d\n", slideSize);
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

                pthread_rwlock_unlock(&slideLock);
            }
        }
        //else printf(" - ACK IGNORATO\n");
    }
}

/* Thread associato al client */
void *client_thread_list(void *args)
{       
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    //close(clientSockFd);
    pthread_exit(NULL);
}

/* Thread associato al client */
void *client_thread_upload(void *args)
{       
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    //close(clientSockFd);
    pthread_exit(NULL);
}

/* Funzione per la chiusura della connessione */
void fin(ClientNode *client) {
	printf("[FIN] -> Partito\n");

    int addrlenClient = sizeof(client -> connection);

    int ackNum;
    int *tmpIntBuff;

    Segment *sndSegment = NULL;

    Segment *rcvSegment = (Segment*) malloc(sizeof(Segment));
    if(rcvSegment == NULL)
    {
        printf("Error while trying to \"malloc\" a new segment for FIN operation!\nClosing...\n");
        exit(-1);
    }

    /* Ricezione FIN */
    //recvSegment(client -> sockfd, rcvSegment, &(client -> connection), &addrlenClient);

    /* Invio ACK del FIN */
    ackNum = atoi(rcvSegment -> seqNum) + 1;
    tmpIntBuff = strToInt(EMPTY);
    newSegment(&sndSegment, FALSE, 2, ackNum, FALSE, TRUE, FALSE, EMPTY, 1, tmpIntBuff);
    sendto(client -> sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&(client -> connection), addrlenClient);

    /* Invio FIN */
    newSegment(&sndSegment, FALSE, 2, ackNum, FALSE, TRUE, TRUE, EMPTY, 1, tmpIntBuff);
    sendto(client -> sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&(client -> connection), addrlenClient);

    /* Ricezione ACK del FIN */
    recvSegment(client -> sockfd, rcvSegment, &(client -> connection), &addrlenClient);

    free(tmpIntBuff);

    close(client -> sockfd);

    pthread_mutex_lock(&lockList);
    deleteClientNode(&clientList, client, &clientListSize, &maxSockFd);
    pthread_mutex_unlock(&lockList);

    printf("\nTrasmissione terminata e disconnessione effettuata con successo!\n");
}