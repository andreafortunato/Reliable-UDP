// https://packetlife.net/blog/2010/jun/7/understanding-tcp-sequence-acknowledgment-numbers/

// Server - FTP on UDP 

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
pthread_rwlock_t lockList;                  /* Semaforo Read/Write necessario per la gestione degli accessi alla lista */
int syncFlag = 0;
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
SegQueue *queueHead = NULL;             // 			W			R/W				(Mutex: queueLock)
Segment rcvWindow[WIN_SIZE];            // 
int rcvWinFlag[WIN_SIZE];               // 
// pthread_mutex_t sndPosLock;				// 
pthread_mutex_t queueLock;				// 			X			X
pthread_rwlock_t slideLock;             // 
pthread_t timeoutTid, sendTid;
int maxTimeout = 5000; // DA CALCOLARE IN BASE ALL'RTT, A CASO 5 (secondi)
int sendQueue;
ClientNode *clientThread;
int addrlenClientThread;
Sockaddr_in clientSocketThread;

/* 32785972 mutex */

// Main - Server 
int main(int argc, char *argv[])
{
    /* Inizializzazione sessione server */
    setbuf(stdout,NULL);
    srand(time(0));
    system("clear");

    /* Gestione del SIGINT (Ctrl+C) */
    signal(SIGINT, ctrl_c_handler);

    /* Inizializzazione semaforo R/W */
    if(pthread_rwlock_init(&lockList, NULL) != 0) {
        printf("Failed semaphore initialization.\n");
        exit(-1);
    }
    
    /* Parse dei parametri passati da riga di comando */
    // int port = parseCmdLine(argc, argv, "server", NULL, &debug);
    // if(port == -1)
    // {
    //     exit(0);
    // }

    maxSeqNum = WIN_SIZE*2;

    fileNameList = getFileNameList(argv[0]+2, &numFiles);

    /* Dichiarazione variabili locali Main */
    int mainSockFd;     /*  */
    int ret;            /*  */

    pthread_t tid;      /*  */

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
        printf("\nServer online...\n\n");
    }
    else {
        printf("\nBinding Failed!\n");
        exit(-1);
    }

    /* Configurazione dimensione buffer di ricezione */
    // int sockBufLen = SOCKBUFLEN;
    // if (setsockopt(mainSockFd, SOL_SOCKET, SO_RCVBUFFORCE, &sockBufLen, sizeof(int)) == -1) {
    //     printf("Error while setting SO_RCVBUF for socket %d: %s\n", mainSockFd, strerror(errno));
    //     exit(-1);
    // }

    /* Server in attesa di richieste da parte dei client */
    while(1) {
        bzero(rcvSegment, sizeof(Segment));
        bzero(threadArgs, sizeof(ThreadArgs));

        recvSegment(mainSockFd, rcvSegment, &clientSocket, &addrlenClient);
        printf("\n[PKT-MAIN]: Received packet from (%s:%d)\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));
        threadArgs = newThreadArgs(clientSocket, *rcvSegment);

        /* Creazione di un thread utile alla fase di handshake */
        ret = pthread_create(&tid, NULL, client_thread_handshake, (void *)threadArgs);
        if(ret != 0)
        {
            printf("New client thread error\n");
            exit(-1);
        }

        /* Attesa dell'aggiunta del nuovo client */
        while(syncFlag == 0);
        syncFlag = 0;
    } 

    /* Distruzione semaforo R/W */
    if(pthread_rwlock_destroy(&lockList) != 0) {
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

/* Thread associato al client */
void *client_thread_handshake(void *args)
{		
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    int clientSockFd;
    int ret;

    int *tmpIntBuff;
    char *tmpStrBuff;

    ThreadArgs *threadArgs = (ThreadArgs*)args;
    printf("\n[TEST]: threadArgs for new client (%s:%d)!\n", inet_ntoa(threadArgs -> clientSocket.sin_addr), ntohs(threadArgs -> clientSocket.sin_port));

    int ackNum = atoi((threadArgs -> segment).seqNum) + 1;

    /* Sockaddr_in client */
    Sockaddr_in clientSocket;
    clientSocket = threadArgs -> clientSocket;
    int addrlenClient = sizeof(clientSocket);

    syncFlag = 1;

    printf("\n[TEST]: threadArgs for client handshake (%s:%d)!\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));

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

    do {
        serverSocket.sin_port = htons(49152 + rand() % (65535+1 - 49152));
    } while(bind(clientSockFd, (struct sockaddr*)&serverSocket, addrlenServer) != 0);
    
    ClientNode *newClient = newNode(clientSockFd, inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port), clientSocket, pthread_self(), ntohs(serverSocket.sin_port), (threadArgs -> segment).seqNum); 

    pthread_rwlock_wrlock(&lockList);
    addClientNode(&clientList, newClient, &clientListSize, &maxSockFd);
    
    printf("New Client\n");
        if(newClient->prev)
            printf("Prev: %d\n", newClient->prev->clientPort);
        else
            printf("Prev: NULL\n");

        printf("Sockfd: %d\nIP: %s\nPort: %d\n", newClient->sockfd, newClient->ip, newClient->clientPort);
        
        if(newClient->next)
            printf("Next: %d\n", newClient->next->clientPort);
        else
            printf("Next: NULL\n");


    pthread_rwlock_unlock(&lockList);

    printf("\n\nThread created for (%s:%d), bind on %d\n", newClient -> ip, newClient -> clientPort, newClient -> serverPort);

    /* SYN-ACK */
    tmpIntBuff = strToInt(EMPTY);
    Segment *synAck = NULL;
    newSegment(&synAck, FALSE, 1, ackNum, TRUE, TRUE, FALSE, EMPTY, 1, tmpIntBuff);

    /* Invio SYN-ACK */
    if((ret = sendto(clientSockFd, synAck, sizeof(Segment), 0, (struct sockaddr*)&clientSocket, addrlenClient)) != sizeof(Segment)) {
        printf("Error trying to send a SYN-ACK segment to client (%s:%d)\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));
        exit(-1);
    }
    else
	   printf("SYN-ACK sent to the client (%s:%d)\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));

	newClient -> lastSeqServer = 1;
    free(synAck);
    free(tmpIntBuff);

    /* ACK del SYN-ACK */
    Segment *rcvSegment = (Segment*) malloc(sizeof(Segment));
    if(rcvSegment == NULL)
    {
        printf("Error while trying to \"malloc\" a new ACK segment of SYN-ACK!\nClosing...\n");
        exit(-1);
    }

    recvSegment(clientSockFd, rcvSegment, &clientSocket, &addrlenClient);
    tmpStrBuff = intToStr(rcvSegment -> msg, atoi(rcvSegment -> lenMsg));
    if(strcmp(tmpStrBuff, EMPTY) == 0) {
        recvSegment(clientSockFd, rcvSegment, &clientSocket, &addrlenClient);
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

        }
    }

    free(rcvSegment);

    pthread_exit(NULL);

    /* 
	 *
	 *
	 * Timeout syn-ack: se dopo X secondi/minuti non ho ricevuto 
	 *
	 *
    */
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
        printf("\nInvio pacchetto - (seqNum: %d) - (msg: %s)\n", atoi(sndSegment -> seqNum), tmpStrBuff);
        bzero(tmpStrBuff, strlen(tmpStrBuff));
        recvSegment(client -> sockfd, rcvSegment, &(client -> connection), &addrlenClient);
        printf("Ricevuto pacchetto - (ackNum: %d)\n", atoi(rcvSegment -> ackNum));
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
	printf("\nFile richiesto: %s\n", fileName);

    /* Se il file è presente nel server */
    if(fileExist(fileNameList, fileName, numFiles)) {
    	pthread_t uselessTid;
    	Segment *sndSegment = NULL;

        // Aprire il file da inviare e scriverlo dentro sndSegment...
        FILE *file = fopen(fileName, "rb");

        char b[1024];
        getcwd(b, 1024);
        printf("%s", b);

        fseek(file, 0, SEEK_END);
        int fileLen = ftell(file);
        fseek(file, 0, SEEK_SET);

        int totalSegs = (fileLen-1)/(LEN_MSG) + 1; /* Formula della vita: (A-1)/B+1, parte intera superiore della divisione A/B */
        printf("\nTotalsegs: %d - fileLen: %d\n", totalSegs, fileLen);

        /* Inizializzazione strutture condivise */
        for(int i=0; i<WIN_SIZE; i++)
            segToSend[i] = 0;

        for(int i=0; i<WIN_SIZE; i++)
            segSent[i] = 0;

        sendQueue = 1;


        /* Inizializzazione semaforo R/W */
        if(pthread_rwlock_init(&slideLock, NULL) != 0) {
            printf("Failed slideLock semaphore initialization.\n");
            exit(-1);
        }

        if(pthread_mutex_init(&queueLock, NULL) != 0) {
        	printf("Failed queueLock semaphore initialization.\n");
            exit(-1);
        }
        


        clientThread = client;
        addrlenClientThread = sizeof(clientThread -> connection);

        int ret;
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

        for(i=0; i<totalSegs; i++) {
           
            bzero(buffFile, LEN_MSG);
            for(j = 1; j <= LEN_MSG; j++, currentPos++) {
                if((ch = fgetc(file)) == EOF)
                    break;
                buffFile[j-1] = htonl(ch);
            }

            if(i == totalSegs-1)
                newSegment(&sndSegment, TRUE, (i%maxSeqNum)+1, -1, FALSE, FALSE, FALSE, "2", j-1, buffFile);
            else
                newSegment(&sndSegment, FALSE, (i%maxSeqNum)+1, -1, FALSE, FALSE, FALSE, "2", j-1, buffFile);

            /* Attendi scorrimento finestra */
            while(sndWinPos >= WIN_SIZE);

            pthread_rwlock_rdlock(&slideLock);

            sndWindow[sndWinPos] = *sndSegment;
            segToSend[sndWinPos] = 1;

            printf("\n[MAIN] -> Caricato pacchetto: (seqNum: %d) - (sndWinPos: %d)\n", atoi(sndSegment -> seqNum), sndWinPos);

            sndWinPos++;

            pthread_rwlock_unlock(&slideLock);
            
        }

        printf("Fine caricamento pacchetti\n");
        fclose(file);
    }
    else {
        printf("\nFile not found!\n");
    }

    pthread_exit(0);
}

void *timeout_thread(void *args) {
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	printf("\n[TIMEOUT] -> TIRATO SU\n");

    int timeoutPos = 0;    

    while(1) {
        // prendi token (qui? in lettura o in scrittura?)
        pthread_rwlock_rdlock(&slideLock);
        if(segSent[timeoutPos] == 1) {
            if(elapsedTime(segTimeout[timeoutPos]) > maxTimeout) {
                
                pthread_mutex_lock(&queueLock);

                printf("\n[TIMEOUT] -> Timeout scaduto: (seqNum: %d) - (timeoutPos: %d)\n", atoi(sndWindow[timeoutPos].seqNum), timeoutPos);

                appendSegToQueue(&queueHead, sndWindow[timeoutPos], timeoutPos);

                pthread_mutex_unlock(&queueLock);
                
            }
            timeoutPos = (timeoutPos+1) % WIN_SIZE;
        }
        pthread_rwlock_unlock(&slideLock);
    }
}


void *continuous_send_thread(void *args) {
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	
	printf("\n[SEND] -> TIRATO SU\n");

    //int sndWinPos = (int*)args;

    SegQueue *prev;

    while(1) {
        pthread_rwlock_rdlock(&slideLock);
        printf("[SEND] -> slideLock preso\n");
        // pthread_mutex_lock(&sndPosLock);

        // usleep(30*1000);

        // inivio intera coda
        while(queueHead != NULL && sendQueue == 1) {
        	prev = queueHead;

            gettimeofday(&segTimeout[queueHead -> winPos], NULL);
            printf("\n[SEND] -> Invio pacchetto in coda: (seqNum: %d) - (winPos: %d)\n", atoi((queueHead -> segment).seqNum), queueHead -> winPos);
            randomSendTo(clientThread -> sockfd, &(queueHead -> segment), (struct sockaddr*)&(clientThread -> connection), addrlenClientThread);

            pthread_mutex_lock(&queueLock);
            queueHead = queueHead -> next;
            free(prev);
            pthread_mutex_unlock(&queueLock);
        }


        
        /* (Primo controllo) Se la recv ha ricevuto un ACK 'corretto', interrompe l'invio della coda (sendQueue=0)
           e resetta lo stato della send;
           (Secondo controllo) Se ho inviato tutta la finestra (ovvero tutti i segToSend==0) resetto 
           lo stato della send */
        if(sendQueue == 0 || segToSend[sndPos] == 0) {
        	// pthread_mutex_unlock(&sndPosLock);
        	pthread_rwlock_unlock(&slideLock);
            continue;
        }

        printf("\n[SEND] -> Invio pacchetto: (seqNum: ");
        printf("%d) - (sndPos: %d)\n", atoi(sndWindow[sndPos].seqNum), sndPos);
        randomSendTo(clientThread -> sockfd, &(sndWindow[sndPos]), (struct sockaddr*)&(clientThread -> connection), addrlenClientThread);
        gettimeofday(&segTimeout[sndPos], NULL);
        segToSend[sndPos] = 0;
        segSent[sndPos] = 1;

        //printf("\nInvio pacchetto - (seqNum: %d)\n", atoi(sndSegment -> seqNum));

        sndPos = (sndPos+1) % WIN_SIZE;
        // pthread_mutex_unlock(&sndPosLock);
        pthread_rwlock_unlock(&slideLock);
    }
}


void *continuous_recv_thread(void *args){
	printf("\n[RECV] -> TIRATO SU\n");

    Segment *rcvSegment = (Segment*)malloc(sizeof(Segment));
    if(rcvSegment == NULL)
    {
        printf("Error while trying to \"malloc\" a new rcvSegment!\nClosing...\n");
        exit(-1);
    }

    int lastAck = 1;
    int countAck = 0;
    bool invalidAck = false;
    int rcvAck;
    int slideSize;

    while(1) {
        recvSegment(clientThread -> sockfd, rcvSegment, &(clientThread -> connection), &addrlenClientThread);
        rcvAck = atoi(rcvSegment -> ackNum);
        printf("\n[RECV] -> Ricevuto ACK: (seqNum: %d) - (ackNum: %d)\n", atoi(rcvSegment -> seqNum), rcvAck);

        if(atoi(rcvSegment -> finBit) == 1) {
        	printf("\n[RECV] -> Ricevuto pacchetto di FIN: (seqNum: %d) - (ackNum: %d)\n", atoi(rcvSegment -> seqNum), rcvAck);
            // TIRA GIU' TUTTO
            pthread_cancel(timeoutTid);
            pthread_cancel(sendTid);
            printf("\n[RECV] -> Cancellati Timeout/Send Thread");
            pthread_join(timeoutTid, NULL);
            printf("\n[RECV] -> Joinato Timeout Thread");
            pthread_join(sendTid, NULL);
            printf("\n[RECV] -> Joinato Send Thread");

            /* Distruzione semafori */
            if(pthread_rwlock_destroy(&slideLock) != 0) {
                printf("Failed slideLock semaphore destruction.\n");
                exit(-1);
            }

            if(pthread_mutex_destroy(&queueLock) != 0) {
                printf("Failed queueLock semaphore destruction.\n");
                exit(-1);
            }


            printf("\n[RECV] -> Sta per partire FIN\n");
            fin(clientThread);
            pthread_exit(0);
        }
        
        /* Se l'ACK ricevuto è inerente ad un pacchetto già riscontrato */
        /* Se l'ACK ricevuto rientra tra uno dei seqNum appartententi a "sndWindow[0].seqNum, sndWindow[-1].seqNum, ..., sndWindow[-(WIN_SIZE-1)].seqNum", ignoriamo l'ACK */
        for(int i = 0; i < WIN_SIZE; i++) {
            invalidAck = invalidAck || (rcvAck == normalize(atoi(sndWindow[0].seqNum), i));
            if (invalidAck)
                break;
        }
        
        if(invalidAck) {
            if(rcvAck == lastAck) {
                if(++countAck == 3) {
                    countAck = 0;
                    // reinvio();
                    // sendto(clientThread -> sockfd, &(SEGMENTO DA INVIARE), sizeof(Segment), 0, (struct sockaddr*)&(clientThread -> connection), addrlenClientThread);
                    continue;
                }
            } else {
                lastAck = rcvAck;
                countAck = 1;
            }
        }
        /* Se l'ACK ricevuto è valido e permette uno slide della finestra (rientra tra sndWindow[1].seqNum, ..., sndWindow[WIN_SIZE].seqNum) */ 
        else {
        	sendQueue = 0;
        	pthread_rwlock_wrlock(&slideLock);
        	printf("\n[RECV] -> Ack valido: (seqNum: %d) - (ackNum: %d)\n", atoi(rcvSegment -> seqNum), rcvAck);

            lastAck = rcvAck;
            countAck = 1;

            // pthread_mutex_lock(&queueLock);
            int maxSeqNumSendable = (atoi(sndWindow[0].seqNum) + WIN_SIZE)%maxSeqNum -1;
            int distance = normalizeDistance(maxSeqNumSendable, rcvAck);

            SegQueue *current = queueHead;
            SegQueue *prev = queueHead;
            while(current != NULL) {
            	prev = current;
            	current = current -> next;
            	if(isSeqMinor(rcvAck, atoi((prev -> segment).seqNum), distance))
            		deleteSegFromQueue(&queueHead, prev);
            		
            		//free(prev);
            }
            // pthread_mutex_unlock(&queueLock);

            // 9 -> 9 10 1 2 3 4 5 6 7 8
            // 7 -> 7 8 9 10 1 2 3 4 5 6
            // 1 -> 1 2 3 4 5

            // WIND:  8 9 10 1 2
            // INDEX: 0 1 2  3 4
            // CODA1: 8 9 10 1 2
            // CODA2: 10 1 2 8 9

            // -1 = (2-1)%10 + 1 = 2

            // -1 = (8-1)%10 + 1 = 8  OK
            // 0  = (8+0)%10 + 1 = 9  OK
            // 1  = (8+1)%10 + 1 = 10  OK
            // 2  = (8+2)%10 + 1 = 1  OK
            // 3  = (8+3)%10 + 1 = 2  OK

            // -1 = (9-1)%10 + 1 = 9  OK
            // 0  = (9+0)%10 + 1 = 10  OK
            // 1  = (9+1)%10 + 1 = 1  OK
            // 2  = (9+2)%10 + 1 = 2  OK

            // -1 = (10-1)%10 + 1 = 10  OK
            // 0  = (10+0)%10 + 1 = 1  OK
            // 1  = (10+1)%10 + 1 = 2  OK

            // -1 = (1-1)%10 + 1 = 1  OK
            // 0  = (1+0)%10 + 1 = 2  OK

            // -1 = (2-1)%10 + 1 = 2  OK

            // Gli altri thread utilizzeranno un rw_lock e il recv potrà cambiare la coda cancellando i pkt fino a quell'ack,
            // ma siamo sicuri che i pacchetti sono aggiunti in ordine? E come li cancelliamo? Come riscontriamo i pacchetti?
            // Segnando i pacchetti fino a quell'ack come riscontrati? Quando ricevo un ack questo è cumulativo, riscontra tutti
            // i precedenti... Nella coda che faccio?
            
            slideSize = normalize(rcvAck, atoi(sndWindow[0].seqNum));

            memmove(sndWindow, &sndWindow[slideSize], sizeof(Segment)*(WIN_SIZE-slideSize));

            memmove(segToSend, &segToSend[slideSize], sizeof(int)*(WIN_SIZE-slideSize));
            memset(&segToSend[WIN_SIZE-slideSize], 0, sizeof(int)*slideSize);

            memmove(segSent, &segSent[slideSize], sizeof(int)*(WIN_SIZE-slideSize));
            memset(&segSent[WIN_SIZE-slideSize], 0, sizeof(int)*slideSize);

            memmove(segTimeout, &segTimeout[slideSize], sizeof(struct timeval)*(WIN_SIZE-slideSize));

            sndWinPos -= slideSize;
            // pthread_mutex_lock(&sndPosLock);

            if(sndPos != 0){
            	sndPos -= slideSize;
            } else {
            	sndPos = sndWinPos;
            }

            // pthread_mutex_unlock(&sndPosLock);

            sendQueue = 1;

            pthread_rwlock_unlock(&slideLock);
        }
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

    deleteClientNode(&clientList, client, &clientListSize, &maxSockFd);
    printf("\nTrasmissione terminata e disconnessione effettuata con successo!\n");
}