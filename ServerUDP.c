// https://packetlife.net/blog/2010/jun/7/understanding-tcp-sequence-acknowledgment-numbers/

// Server - FTP on UDP 

//////////////////////////////////////////////////////////////////////////
// sha256sum -z cuore.png | cut -d " " -f 1 --> d92d0c0bd9977bdfe8941302ddc6ab940aa2958e6bc9156937d5f25c8a6e781c
//////////////////////////////////////////////////////////////////////////

#include <time.h>
#include <stdbool.h>
#include "Server.h"

#define IP_PROTOCOL 0

/* Prototipi */
void ctrl_c_handler();

int list(ClientNode *);
int download(ClientNode*, char *);
int upload(ClientNode*, char *);

void fin(ClientNode *);

void *client_thread_handshake(void *);      /* Thread associato al client */
void *timeout_thread(void *);
void *continuous_send_thread(void *);
void *continuous_recv_thread(void *);

/* Variabili globali */
int syncFlag;
int debug = 0;                             /* Se l'utente ha avviato in modalità debug, verranno mostrate informazioni aggiuntive */

ClientNode *clientList = NULL;
pthread_mutex_t clientListLock;            /* Semaforo Read/Write necessario per la gestione degli accessi alla lista */

FileNode *fileListHead = NULL;
pthread_rwlock_t fileListLock;
char *fileListStr = NULL;

int maxSeqNum;
float loss_prob = LOSS_PROB;
int is_loss_prob_default = 1;

// Main - Server 
int main(int argc, char *argv[])
{
    /* Inizializzazione sessione server */
    setbuf(stdout,NULL);
    srand(time(0));
    system("clear");

    /* Gestione del SIGINT (Ctrl+C) */
    signal(SIGINT, ctrl_c_handler);

    /* Inizializzazione semafori */
    if(pthread_mutex_init(&clientListLock, NULL) != 0) {
        printf("Failed semaphore initialization.\n");
        exit(-1);
    }
    if(pthread_rwlock_init(&fileListLock, NULL) != 0) {
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

    getFileList(&fileListHead, argv[0]+2);
    fileListStr = fileNameListToString(fileListHead);

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
        pthread_mutex_lock(&clientListLock);
        checkClient = clientList;
        while(checkClient != NULL) {
            if(strcmp(checkClient->ip, inet_ntoa(clientSocket.sin_addr)) == 0 && checkClient->clientPort == ntohs(clientSocket.sin_port)) {
                printf("Il client (%s:%d) era già connesso\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));
                break;
            }
            else
                checkClient = checkClient -> next;
        }
        if (checkClient != NULL) {
            printf("Termino il client in questione.\n");

            pthread_cancelAndWait(checkClient, checkClient -> handTid);
            pthread_cancelAndWait(checkClient, checkClient -> timeTid);
            pthread_cancelAndWait(checkClient, checkClient -> sendTid);
            pthread_cancelAndWait(checkClient, checkClient -> recvTid);

            deleteClientNode(&clientList, checkClient);
        }
        // pthread_mutex_unlock(&clientListLock);
        if(pthread_mutex_unlock(&clientListLock) != 0) {
            printf("%d : %s\n", errno, strerror(errno));
            exit(-1);
        }

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
    if(pthread_mutex_destroy(&clientListLock) != 0) {
        printf("Failed semaphore destruction.\n");
        exit(-1);
    }

    return 0; 
}

void ctrl_c_handler()
{
    if(is_loss_prob_default) {
        loss_prob = 50;
        is_loss_prob_default = 0;
    } else {
        loss_prob = LOSS_PROB;
        is_loss_prob_default = 1;
    }
    
    printf("New loss_prob: %.2f\n", loss_prob);
    // printf("\n\n---> STAMPA LISTA FILE <---\n\n");
    // printf("Number of files: %d\n", numFiles);
    // printf("%s", fileNameListToString(fileListHead));
    
    // printf("\n\n---> STAMPA LISTA <---\n\n");
    // printf("|Size: %d|\n", clientListSize);

    // pthread_mutex_lock(&clientListLock);
    // printList(clientList);
    // pthread_mutex_unlock(&clientListLock);

    // int sock;
    // printf("\nSocket da eliminare: ");
    // scanf("%d", &sock);

    // ClientNode *tmp = clientList;

    // while(tmp != NULL)
    // {
    //     if(tmp -> sockfd == sock)
    //         deleteClientNode(&clientList, tmp);

    //     tmp = tmp -> next;
    // }

    // printf("\n\n---> STAMPA LISTA <---\n\n");
    // printf("|Size: %d|\n", clientListSize);
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
        serverSocket.sin_port = 0;
    } while(bind(clientSockFd, (struct sockaddr*)&serverSocket, addrlenServer) != 0);

    struct sockaddr_in serverPort;
    socklen_t len = sizeof(serverPort);
    if (getsockname(clientSockFd, (struct sockaddr *)&serverPort, &len) == -1)
        printf("getsockname error\n");
    
    /* Aggiunta alla lista dei client sul server del client appena connesso */
    ClientNode *newClient = newNode(clientSockFd, clientSocket, pthread_self(), ntohs(serverPort.sin_port)); 
    pthread_mutex_lock(&clientListLock);
    addClientNode(&clientList, newClient);
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
    // pthread_mutex_unlock(&clientListLock);
    if(pthread_mutex_unlock(&clientListLock) != 0) {
        printf("%d : %s\n", errno, strerror(errno));
        exit(-1);
    }

    /* Generazione segmento SYN-ACK */
    tmpIntBuff = strToInt(EMPTY);
    Segment *synAck = NULL;
    newSegment(&synAck, FALSE, 1, atoi((threadArgs.segment).seqNum)%maxSeqNum + 1, TRUE, TRUE, FALSE, EMPTY, 1, tmpIntBuff);
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

    if (setsockopt(newClient -> sockfd, SOL_SOCKET, SO_RCVTIMEO, &synTimeout, sizeof(synTimeout)) < 0) {
            printf("\nError while setting syn-ack-timeout");
            exit(-1);
    }

    gettimeofday(&synTimeout, NULL);

    // sendto(newClient -> sockfd, synAck, sizeof(Segment), 0, (struct sockaddr*)&(newClient -> connection), newClient -> addrlenSocket);
    // printf("SYN-ACK sent to the client (%s:%d)\n", newClient -> ip, newClient -> clientPort);
    /* Invio SYN-ACK */
    if(randomSendTo(newClient -> sockfd, synAck, (struct sockaddr*)&(newClient -> connection), (newClient -> addrlenSocket), loss_prob)){
        // printf("SYN-ACK sent to the client (%s:%d)\n", newClient -> ip, newClient -> clientPort);
    }
    else{
        // printf("SYN-ACK LOST\n");
    }
    free(synAck);

    /* Attesa ricezione dell'ack del syn-ack per un massimo di 30 secondi, dopodichè
       chiusura della connessione verso quel client */
    while(1) {
        checkIfMustDie(newClient, HANDSHAKE);
        if(elapsedTime(synTimeout) > 30*1000) {
            free(rcvSegment);
            printf("[SYN] Client %s:%d is dead.\n", newClient -> ip, newClient -> clientPort);

            pthread_mutex_lock(&clientListLock);
            deleteClientNode(&clientList, newClient);
            // pthread_mutex_unlock(&clientListLock);
            if(pthread_mutex_unlock(&clientListLock) != 0) {
                printf("%d : %s\n", errno, strerror(errno));
                exit(-1);
            }

            pthread_exit(NULL);
        }

        /* Controllo che il segmento ricevuto sia un ACK (oppure ACK+operazione), in caso
           negativo ignoro il segmento e ne attendo un altro */
        if(recvSegment(newClient -> sockfd, rcvSegment, &(newClient -> connection), &(newClient -> addrlenSocket)) < 0 || atoi(rcvSegment -> ackBit) != 1) 
            continue;

        // printf("\n[SYN] -> Ricevuta richiesta operazione: %d\n", atoi(rcvSegment -> cmdType));

        break;       
    }


    /* Imposto un timeout di 30 secondi per la ricezione della richiesta di operazione da parte del client */
    gettimeofday(&synTimeout, NULL);

    /* Se il pacchetto ricevuto è l'ACK del SYN-ACK, attendo il prossimo per la richiesta di operazione */
    if(strcmp(rcvSegment -> cmdType, EMPTY) == 0) {
        while(1) {
            checkIfMustDie(newClient, HANDSHAKE);
            /* Ricezione operazione */
            if(recvSegment(newClient -> sockfd, rcvSegment, &(newClient -> connection), &(newClient -> addrlenSocket)) < 0) {
                if(elapsedTime(synTimeout) > 30*1000) {
                    synTimeout.tv_usec = 0;
                    synTimeout.tv_sec = 0;
                    if (setsockopt(newClient -> sockfd, SOL_SOCKET, SO_RCVTIMEO, &synTimeout, sizeof(synTimeout)) < 0) {
                        printf("\nError while setting syn-ack-timeout");
                        exit(-1);
                    }

                    pthread_mutex_lock(&clientListLock);
                    deleteClientNode(&clientList, newClient);
                    // pthread_mutex_unlock(&clientListLock);
                    if(pthread_mutex_unlock(&clientListLock) != 0) {
                        printf("%d : %s\n", errno, strerror(errno));
                        exit(-1);
                    }

                    free(rcvSegment);

                    pthread_exit(NULL);
                }
            } else
                break;
        }
    }


    /* Reset del timeout per la recvfrom */
    synTimeout.tv_usec = 0;
    synTimeout.tv_sec = 0;
    if (setsockopt(newClient -> sockfd, SOL_SOCKET, SO_RCVTIMEO, &synTimeout, sizeof(synTimeout)) < 0) {
        printf("\nError while setting syn-ack-timeout");
        exit(-1);
    }

    //clientThread = newClient;
    //addrlenClientThread = sizeof(clientThread -> connection);
    int ret;

    /* Switch operazioni richieste dal client */
    switch(atoi(rcvSegment -> cmdType)) {
        case 1:
            if(list(newClient) == 0)
                fin(newClient);

            newClient -> mustDie[TIMEOUT] = 1;
            newClient -> mustDie[SEND] = 1;
            pthread_join(newClient -> timeTid, NULL);
            pthread_join(newClient -> sendTid, NULL);

            break;

        case 2:
            tmpStrBuff = intToStr(rcvSegment -> msg, atoi(rcvSegment -> lenMsg));

            ret = download(newClient, tmpStrBuff);
            if(ret == 0  ||  ret == -2){
                fin(newClient);
            }

            if(ret == 0 || ret == -1) {
                newClient -> mustDie[TIMEOUT] = 1;
                newClient -> mustDie[SEND] = 1;
                pthread_join(newClient -> timeTid, NULL);
                pthread_join(newClient -> sendTid, NULL);
            }

            free(tmpStrBuff);
            
            break;

        case 3:
            break;

        default:

            break;

    }

    pthread_mutex_lock(&clientListLock);
    deleteClientNode(&clientList, newClient);
    // pthread_mutex_unlock(&clientListLock);
    if(pthread_mutex_unlock(&clientListLock) != 0) {
        printf("%d : %s\n", errno, strerror(errno));
        exit(-1);
    }

    free(rcvSegment);

    pthread_exit(NULL);
}

/* Funzione list */
int list(ClientNode *client)
{   
    int ret;
    Segment *sndSegment = NULL;

    int lenBuff;

    lenBuff = strlen(fileListStr);
    
    int totalSegs = (lenBuff-1)/(LEN_MSG) + 1; /* Formula della vita: (A-1)/B+1, parte intera superiore della divisione A/B */

    ret = pthread_create(&(client -> timeTid), NULL, timeout_thread, (void*)client);
    if(ret != 0)
    {
        printf("New timeout thread error\n");
        exit(-1);
    }
    ret = pthread_create(&(client -> sendTid), NULL, continuous_send_thread, (void*)client);
    if(ret != 0)
    {
        printf("New continuous send thread error\n");
        exit(-1);
    }
    ret = pthread_create(&(client -> recvTid), NULL, continuous_recv_thread, (void*)client);
    if(ret != 0)
    {
        printf("New continuous recv thread error\n");
        exit(-1);
    }

    int buffFile[LEN_MSG];
    int i, j;
    int currentPos = 1; 

    /* Caricamento del primo pacchetto contenente \bTOTALSEGS\bNOME\b */  
    char tmpStrBuff[LEN_MSG];
    int *tmpIntBuff;

    sprintf(tmpStrBuff, "\b%d\b", totalSegs+1);
    tmpIntBuff = strToInt(tmpStrBuff);
    newSegment(&sndSegment, FALSE, 1, -1, FALSE, FALSE, FALSE, "1", strlen(tmpStrBuff), tmpIntBuff);

    (client -> sndWindow)[(client -> sndWinPos)] = *sndSegment;
    (client -> segToSend)[(client -> sndWinPos)] = 1;
    (client -> sndWinPos)++;

    free(tmpIntBuff);

    tmpIntBuff = malloc(sizeof(int)*LEN_MSG);
    if(tmpIntBuff == NULL)
    {
        printf("Error while trying to \"malloc\" a new tmpIntBuff!\nClosing...\n");
        exit(-1);
    }

    /* Caricamento segmenti da inviare nella finestra sndWindow */
    for(i=1; i<=totalSegs; i++) {
        checkIfMustDie(client, HANDSHAKE);
        
        /* Lettura di LEN_MSG caratteri convertiti in formato network e memorizzati in buffFile (Endianness problem) */
        bzero(buffFile, LEN_MSG);
        for(j = 1; (j <= LEN_MSG) && (currentPos < lenBuff); j++, currentPos++) {
            tmpIntBuff[j-1] = htonl((int)fileListStr[currentPos-1]);
        }

        /* Se è l'ultimo segmento imposta End-Of-Transmission-Bit a 1 */
        if(i == totalSegs)
            newSegment(&sndSegment, TRUE, (i%maxSeqNum)+1, -1, FALSE, FALSE, FALSE, "1", j-1, tmpIntBuff);
        else
            newSegment(&sndSegment, FALSE, (i%maxSeqNum)+1, -1, FALSE, FALSE, FALSE, "1", j-1, tmpIntBuff);

        /* Attendi slide finestra per il caricamento di altri segmenti */
        while((client -> sndWinPos) >= WIN_SIZE) {
            checkIfMustDie(client, HANDSHAKE);
        }

        pthread_rwlock_rdlock(&(client -> slideLock));

        (client -> sndWinPos)++;

        (client -> sndWindow)[(client -> sndWinPos)-1] = *sndSegment;
        (client -> segToSend)[(client -> sndWinPos)-1] = 1;

        // printf("\n[LIST] -> Caricato pacchetto: (seqNum: %d) - ((client -> sndWinPos): %d)\n", atoi(sndSegment -> seqNum), (client -> sndWinPos)-1);

        // pthread_rwlock_unlock(&(client -> slideLock));
        if(pthread_rwlock_unlock(&(client -> slideLock)) != 0) {
            printf("%d : %s\n", errno, strerror(errno));
            exit(-1);
        }
    }

    printf("Fine caricamento pacchetti\n");
    free(tmpIntBuff);

    int *joinRet;

    pthread_join(client -> recvTid, (void**)&joinRet);
    
    printf("VALORE RITORNATO DALLA JOIN: %d\n", *joinRet);
    if(*joinRet == -1)
        return -1;

    return 0;
}

/* Funzione download */
int download(ClientNode *client, char *fileName)
{   
    char *originalFileName = fileExist(fileListHead, fileName);

    Segment *sndSegment = NULL;
    int *tmpIntBuff;

    /* Se il file è presente nel server */
    if(originalFileName != NULL) {

        int ret;
        int *joinRet;

        /* Apertura del file richiesto dal client */
        FILE *file = fopen(originalFileName, "rb");
        client -> fileDescriptor = file;

        // char b[1024];
        // getcwd(b, 1024);
        // printf("%s", b);

        fseek(file, 0, SEEK_END);
        int fileLen = ftell(file);
        fseek(file, 0, SEEK_SET);

        /* Formula della vita: (A-1)/B+1, parte intera superiore della divisione A/B */
        int totalSegs = (fileLen-1)/(LEN_MSG) + 1;

        ret = pthread_create(&(client -> timeTid), NULL, timeout_thread, (void*)client);
        if(ret != 0)
        {
            printf("New timeout thread error\n");
            exit(-1);
        }
        ret = pthread_create(&(client -> sendTid), NULL, continuous_send_thread, (void*)client);
        if(ret != 0)
        {
            printf("New continuous send thread error\n");
            exit(-1);
        }
        ret = pthread_create(&(client -> recvTid), NULL, continuous_recv_thread, (void*)client);
        if(ret != 0)
        {
            printf("New continuous recv thread error\n");
            exit(-1);
        }

        // printf("Hand: %ld - Time: %ld - Send: %ld - Recv: %ld\n", client -> handTid, client -> timeTid, client -> sendTid, client -> recvTid);

        int buffFile[LEN_MSG];
        int ch;
        int i, j;
        int currentPos = 1; 

        /* Caricamento del primo pacchetto contenente \bTOTALSEGS\bNOME\b */  
        char tmpStrBuff[LEN_MSG];
        char *fileSHA256 = getFileSHA256(originalFileName);


        sprintf(tmpStrBuff, "\b%d\b%s\b%d\b%s\b", totalSegs+1, originalFileName, fileLen, fileSHA256);
        tmpIntBuff = strToInt(tmpStrBuff);
        newSegment(&sndSegment, FALSE, 1, -1, FALSE, FALSE, FALSE, "2", strlen(tmpStrBuff), tmpIntBuff);

        (client -> sndWindow)[(client -> sndWinPos)] = *sndSegment;
        (client -> segToSend)[(client -> sndWinPos)] = 1;
        (client -> sndWinPos)++;

        free(tmpIntBuff);

        /* Caricamento segmenti da inviare nella finestra sndWindow */
        for(i=1; i<=totalSegs; i++) {
            checkIfMustDie(client, HANDSHAKE);
            
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
            while((client -> sndWinPos) >= WIN_SIZE) {
                checkIfMustDie(client, HANDSHAKE);
                
                if(client -> recvDead) {
                    fclose(file);
                    client -> fileDescriptor = NULL;
                    
                    pthread_join(client -> recvTid, (void**)&joinRet);
                    printf("VALORE RITORNATO DALLA JOIN: %d\n", *((int*)joinRet));
                    if(*((int*)joinRet) == -1)
                        return -1;
                }
            }

            pthread_rwlock_rdlock(&(client -> slideLock));

            (client -> sndWinPos)++;

            (client -> sndWindow)[(client -> sndWinPos)-1] = *sndSegment;
            (client -> segToSend)[(client -> sndWinPos)-1] = 1;

            // printf("\n[MAIN] -> Caricato pacchetto: (seqNum: %d) - (sndWinPos: %d)\n", atoi(sndSegment -> seqNum), (client -> sndWinPos)-1);

            // pthread_rwlock_unlock(&(client -> slideLock));
            if(pthread_rwlock_unlock(&(client -> slideLock)) != 0) {
                printf("%d : %s\n", errno, strerror(errno));
                exit(-1);
            }
        }

        //printf("Fine caricamento pacchetti\n");
        fclose(file);
        client -> fileDescriptor = NULL;

        pthread_join(client -> recvTid, (void**)&joinRet);
        if(*joinRet == -1)
            return -1;

        return 0;
    }
    else {
        /* Se il file non è presente sul server notifico il client */
        printf("\nFile not found!\n");

        int countRetransmission = 1;

        Segment *rcvSegment = (Segment*)malloc(sizeof(Segment));
        if(rcvSegment == NULL)
        {
            printf("Error while trying to \"malloc\" a new rcvSegment!\nClosing...\n");
            exit(-1);
        }

        tmpIntBuff = strToInt(EMPTY);
        newSegment(&sndSegment, TRUE, 1, -1, FALSE, FALSE, FALSE, "2", 1, tmpIntBuff);
        free(tmpIntBuff);

        /* Timer associato al messaggio FNF (FileNotFound) */
        struct timeval FNFTimeout;
        FNFTimeout.tv_usec = 0;
        do {
            checkIfMustDie(client, HANDSHAKE);
            /* Tenta la ritrasmissione al massimo 7 volte incrementando di volta in volta il timeout (tempo totale massimo 29 secondi) */
            switch(countRetransmission) {
                case 1:
                    FNFTimeout.tv_sec = 1;
                    break;

                case 2:
                    FNFTimeout.tv_sec = 3;
                    break;

                case 8:
                    FNFTimeout.tv_sec = 0;  // Reset del timeout a 0 (= attendi all'infinito)

                    if (setsockopt((client -> sockfd), SOL_SOCKET, SO_RCVTIMEO, &FNFTimeout, sizeof(FNFTimeout)) < 0) {
                        printf("\nError while setting FNF-timeout at try n°: %d\n", countRetransmission);
                        exit(-1);
                    }

                    free(rcvSegment);
                    free(sndSegment);
                    
                    return -1;
                    break;

                default:
                    FNFTimeout.tv_sec = 5;
                    break;
            }

            if (setsockopt((client -> sockfd), SOL_SOCKET, SO_RCVTIMEO, &FNFTimeout, sizeof(FNFTimeout)) < 0) {
                printf("\nError while setting FNF-timeout at try n°: %d\n", countRetransmission);
                exit(-1);
            }

            printf("\nFileNotFound send n°%d...\n", countRetransmission);
            // sendto((client -> sockfd), sndSegment, sizeof(Segment), 0, (struct sockaddr*)&(client -> connection), (client -> addrlenSocket));
            // printf("\n[DOWNLOAD]: FileNotFound sent to the client (%s:%d)\n", (client -> ip), (client -> clientPort));
            /* Invio FNF */
            if(randomSendTo((client -> sockfd), sndSegment, (struct sockaddr*)&(client -> connection), (client -> addrlenSocket), loss_prob))
                printf("\n[DOWNLOAD]: FileNotFound sent to the client (%s:%d)\n", (client -> ip), (client -> clientPort));
            else
                printf("\n[DOWNLOAD]: FileNotFound LOST\n");

            countRetransmission += 1;
        } while(recvSegment((client -> sockfd), rcvSegment, &(client -> connection), &(client -> addrlenSocket)) < 0 || (atoi(rcvSegment -> finBit) != 1 && (atoi(rcvSegment -> ackBit) != 1 || atoi(rcvSegment -> cmdType) != 2)));        

        
        gettimeofday(&FNFTimeout, NULL);

        /* Aspetto la richiesta di FIN per un massimo di 30 secondi */
        /* Se il pacchetto ricevuto è l'ACK del FileNotFound, attendo la richiesta di FIN */
        if(strcmp(rcvSegment -> cmdType, "2") == 0) {
            while(1) {
                checkIfMustDie(client, HANDSHAKE);
                /* Ricezione operazione */
                if(recvSegment(client -> sockfd, rcvSegment, &(client -> connection), &(client -> addrlenSocket)) < 0) {
                    if(elapsedTime(FNFTimeout) > 30*1000) {
                        FNFTimeout.tv_sec = 0;
                        FNFTimeout.tv_usec = 0;

                        if (setsockopt((client -> sockfd), SOL_SOCKET, SO_RCVTIMEO, &FNFTimeout, sizeof(FNFTimeout)) < 0) {
                            printf("\nError while setting fin-timeout at try n°: %d\n", countRetransmission);
                            exit(-1);
                        }

                        free(sndSegment);
                        free(rcvSegment);

                        return -3;
                    }
                } else
                    break;
            }
        }

        FNFTimeout.tv_sec = 0;
        FNFTimeout.tv_usec = 0;

        if (setsockopt((client -> sockfd), SOL_SOCKET, SO_RCVTIMEO, &FNFTimeout, sizeof(FNFTimeout)) < 0) {
            printf("\nError while setting fin-timeout at try n°: %d\n", countRetransmission);
            exit(-1);
        }

        return -2;
    }
}

void *timeout_thread(void *args) {
	
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);


    ClientNode *client = (ClientNode*) args;


    int timeoutPos = 0;    
    int maxSeqNumSendable;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    tv.tv_sec += 86400; // Per impostare un timeout molto elevato, nel "futuro"

    while(1) {
        checkIfMustDie(client, TIMEOUT);

        pthread_rwlock_rdlock(&(client -> slideLock));

        /* Se il segmento sndWindow[timeoutPos] è stato inviato allora controlla il suo timeout */
        if((client -> segSent)[timeoutPos] == 1) {
            /* Se il timeout per il segmento in esame è scaduto */
            if(elapsedTime((client -> segTimeout)[timeoutPos]) > 100) {
                
                pthread_mutex_lock(&(client -> queueLock));

                /*************************************************************************************************************************/
                // printf("\n[TIMEOUT] -> Timeout scaduto: (seqNum: %d) - (timeoutPos: %d)\n", atoi((client -> sndWindow)[timeoutPos].seqNum), timeoutPos);
                /**************************************************************************************************************************/

                (client -> segTimeout)[timeoutPos] = tv;

                //printf("sndWindow[0].seqNum: %s - WIN_SIZE: %d - maxSeqNum: %d ", sndWindow[0].seqNum, WIN_SIZE, maxSeqNum);
                maxSeqNumSendable = (atoi((client -> sndWindow)[0].seqNum) + WIN_SIZE-2)%(maxSeqNum) + 1;
                //printf("SNDWINDOW[0]: %s - maxSeqNumSendable: %d - ", sndWindow[0].seqNum, maxSeqNumSendable);
                orderedInsertSegToQueue(&(client -> queueHead), (client -> sndWindow)[timeoutPos], timeoutPos, maxSeqNumSendable);

                // pthread_mutex_unlock(&(client -> queueLock));  
                if(pthread_mutex_unlock(&(client -> queueLock)) != 0) {
                    printf("%d : %s\n", errno, strerror(errno));
                    exit(-1);
                }
            }
        }

        timeoutPos = (timeoutPos+1) % WIN_SIZE;

        // pthread_rwlock_unlock(&(client -> slideLock));
        if(pthread_rwlock_unlock(&(client -> slideLock)) != 0) {
            printf("%d : %s\n", errno, strerror(errno));
            exit(-1);
        }
    }
}

void *continuous_send_thread(void *args) {

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);


    ClientNode *client = (ClientNode*)args;

	
    SegQueue *prev;

    while(1) {
        checkIfMustDie(client, SEND);
        /* Attesa sblocco coda di trasmissione da parte del thread recv */
        if((client -> sendQueue) == 0)
            continue;

        pthread_rwlock_rdlock(&(client -> slideLock));

        /* Invio dell'intera coda */
        while((client -> queueHead) != NULL && (client -> sendQueue) == 1) {
            checkIfMustDie(client, SEND);

            pthread_mutex_lock(&(client -> queueLock));

            //printf("\n[SEND] -> Invio pacchetto in coda: (seqNum: %d) - (winPos: %d)\n", atoi(((client -> queueHead) -> segment).seqNum), (client -> queueHead) -> winPos);
            /*************************************************************************************************************************/
            //printf("\nsegTimeout[%d]: %ld\n", (client -> queueHead) -> winPos, (client -> segTimeout)[(client -> queueHead) -> winPos].tv_sec);
            /**************************************************************************************************************************/
            // printf("[SEND-Q] -> %d - %s:%d - from: %d\n", client -> sockfd, client -> ip, client -> clientPort, client -> serverPort);
            if(randomSendTo(client -> sockfd, &((client -> queueHead) -> segment), (struct sockaddr*)&(client -> connection), (client -> addrlenSocket), loss_prob) == 1) {
                // printf("\n[RAND_SENDTO-QUEUE] -> pacchetto inviato seqNum: "); //printf("%d\n", atoi(((client -> queueHead) -> segment).seqNum));
                // if(client == NULL) {
                //     printf("client NULL\n");
                //     exit(-1);
                // }else if((client -> queueHead) == NULL) {
                //     printf("(client -> queueHead) NULL\n");
                //     exit(-1);
                // }else {
                //     printf("%d\n", atoi(((client -> queueHead) -> segment).seqNum));
                // }
            }
            else {
                // printf("\n[RAND_SENDTO-QUEUE] -> pacchetto perso seqNum: "); printf("%d\n", atoi(((client -> queueHead) -> segment).seqNum));
            }
            /**************************************************************************************************************************/

            /* Imposta il nuovo timestamp */
            gettimeofday(&(client -> segTimeout)[(client -> queueHead) -> winPos], NULL);

            /* Controllo se il segmento queueHead -> segment è già stato riscontrato allora 
               resettiamo il timestamp per il calcolo di un nuovo rtt */
            if((client -> rttFlag)[(client -> queueHead) -> winPos] == 1) {
                gettimeofday(&(client -> segRtt)[(client -> queueHead) -> winPos], NULL);
                (client -> rttFlag)[(client -> queueHead) -> winPos] = 0;
            }
            
            prev = (client -> queueHead);
            (client -> queueHead) = (client -> queueHead) -> next;
            free(prev);

            // pthread_mutex_unlock(&(client -> queueLock));
            if(pthread_mutex_unlock(&(client -> queueLock)) != 0) {
                printf("%d : %s\n", errno, strerror(errno));
                exit(-1);
            }
        }
       
        /* (Primo controllo) Se la recv ha ricevuto un ACK 'corretto', interrompe l'invio della coda (sendQueue=0)
           e resetta lo stato della send;
           (Secondo controllo) Se ho inviato tutta la finestra (ovvero tutti i segToSend sono uguali a 0) resetto 
           lo stato della send */
        if((client -> sendQueue) == 0 || (client -> segToSend)[(client -> sndPos)] == 0) {
        	// pthread_rwlock_unlock(&(client -> slideLock));
            if(pthread_rwlock_unlock(&(client -> slideLock)) != 0) {
                printf("%d : %s\n", errno, strerror(errno));
                exit(-1);
            }
            continue;
        }

        /* Impostiamo il timestamp per il calcolo dell'rtt */
        gettimeofday(&(client -> segRtt)[(client -> sndPos)], NULL);
        
        // printf("[SEND] -> %d - %s:%d - from: %d\n", client -> sockfd, client -> ip, client -> clientPort, client -> serverPort);
        if(randomSendTo(client -> sockfd, &((client -> sndWindow)[(client -> sndPos)]), (struct sockaddr*)&(client -> connection), (client -> addrlenSocket), loss_prob) == 1) {
            // printf("\n[RAND_SENDTO] -> pacchetto inviato seqNum: %d - (sndPos: %d)\n", atoi((client -> sndWindow)[(client -> sndPos)].seqNum), (client -> sndPos));
        }
        else {
            // printf("\n[RAND_SENDTO] -> pacchetto perso seqNum: %d - (sndPos: %d)\n", atoi((client -> sndWindow)[(client -> sndPos)].seqNum), (client -> sndPos));
        }
        
        /* Impostiamo il timestamp per il calcolo del timeout */
        gettimeofday(&(client -> segTimeout)[(client -> sndPos)], NULL);

        (client -> segToSend)[(client -> sndPos)] = 0;
        (client -> segSent)[(client -> sndPos)] = 1;

        (client -> sndPos) = ((client -> sndPos)+1) % WIN_SIZE;

        // pthread_rwlock_unlock(&(client -> slideLock));
        if(pthread_rwlock_unlock(&(client -> slideLock)) != 0) {
            printf("%d : %s\n", errno, strerror(errno));
            exit(-1);
        }
    }
}

void *continuous_recv_thread(void *args){
    
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);


    ClientNode *client = (ClientNode*)args;

	
    RTT_Data *rttData = initData();

    Segment *rcvSegment = (Segment*)malloc(sizeof(Segment));
    if(rcvSegment == NULL)
    {
        printf("Error while trying to \"malloc\" a new rcvSegment!\nClosing...\n");
        exit(-1);
    }

    int *joinRet = malloc(1*sizeof(int));

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

    struct timeval deadLineTimeout;
    struct timeval repeatTimeout;
    repeatTimeout.tv_sec = 0;
    repeatTimeout.tv_usec = 100*1000;

    if (setsockopt(client -> sockfd, SOL_SOCKET, SO_RCVTIMEO, &repeatTimeout, sizeof(repeatTimeout)) < 0) {
        printf("\nError while setting repeatTimeout-timeout\n");
        exit(-1);
    }

    gettimeofday(&deadLineTimeout, NULL);
    while(1) {
        checkIfMustDie(client, RECV);
        // printf("sockfd: %d\n", client -> sockfd);
        // printf("%s:%d\n", client -> ip, client -> clientPort);
        // printf("len: %d\n", (client -> addrlenSocket));
        if(recvSegment(client -> sockfd, rcvSegment, &(client -> connection), &(client -> addrlenSocket)) < 0) {
            if(elapsedTime(deadLineTimeout) > 60*1000) {
                printf("RECV FALLITA\n");

                // pthread_cancelAndWait(client, client -> timeTid);
                // pthread_cancelAndWait(client, client -> sendTid);

                // Free della struttura dati thread
                *joinRet = -1;
                printf("Ora termino\n");
                client -> recvDead = 1;
                pthread_exit(joinRet);
            }
            else
                continue;
        }
        gettimeofday(&deadLineTimeout, NULL);

        tmpStrBuff = intToStr(rcvSegment -> msg, atoi(rcvSegment -> lenMsg));
        rcvAck = atoi(rcvSegment -> ackNum);
        rcvAckedSeq = atoi(tmpStrBuff);
        // printf("\n[RECV] -> Ricevuto ACK: (seqNum: %d) - (ackNum: %d) - (seqNumAcked: %d)\n", atoi(rcvSegment -> seqNum), rcvAck, rcvAckedSeq);
        free(tmpStrBuff);

        /* Se l'ack ricevuto ha il fin bit impostato ad 1 allora il client ha ricevuto tutto e
           ha chiesto la chiusura della connessione */
        if(atoi(rcvSegment -> finBit) == 1) {
            //printf("\n[RECV] -> Ricevuto pacchetto di FIN: (seqNum: %d) - (ackNum: %d)\n", atoi(rcvSegment -> seqNum), rcvAck);
            checkIfMustDie(client, RECV);
            
            // pthread_cancelAndWait(client, client -> timeTid);
            // pthread_cancelAndWait(client, client -> sendTid);

            // Free della struttura dati thread
            *joinRet = 0;
            pthread_exit(joinRet);
        }

        /* Se c'è almeno un segmento caricato processa l'ack */
        if((client -> sndWinPos) != 0) {

            /* Aggiornamento dell'RTO */
            rttPos = normalizeDistance(rcvAckedSeq, atoi((client -> sndWindow)[0].seqNum));
            if(rttPos < WIN_SIZE) {
                //printf("\nRTT_FLAG[%d] = %d - %d\n", rttPos, (client -> rttFlag)[rttPos], atoi((client -> sndWindow)[0].seqNum));
                if((client -> rttFlag)[rttPos] == 0) {
                    (client -> RTO) = calculateRTO((client -> segRtt)[rttPos], rttData);
                    (client -> rttFlag)[rttPos] = 1;
                }
            }

            /* Calcola se l'ACK ricevuto è inerente ad un pacchetto già riscontrato 
               e/o rientra tra uno dei seqNum appartententi a 
               "sndWindow[0].seqNum, sndWindow[-1].seqNum, ..., sndWindow[-(WIN_SIZE-1)].seqNum" */
            printf("rcvAck: %d, seqNum: %d", rcvAck, atoi((client -> sndWindow)[0].seqNum));
            invalidAck = false;
            for(int i = 0; i < WIN_SIZE; i++) {
                invalidAck = invalidAck || (rcvAck == normalize(atoi((client -> sndWindow)[0].seqNum), i));
                if(invalidAck)
                    break;
            }
            printf(", invalid: %d\n", invalidAck);

            if(invalidAck) {
                //printf(" - Ack non valido\n");
                if(rcvAck == lastAck) {
                    /* Meccanismo del Fast Retransmit: aggiunta del segmento in testa alla coda di ritrasmissione */
                    if(++countAck == 3) {
                        pthread_rwlock_wrlock(&(client -> slideLock));
                        //printf("\n[RECV] -> Fast Retransmit for seqNum: %d\n", rcvAck);
                        countAck = 0;
                        if((client -> queueHead) == NULL || atoi(((client -> queueHead) -> segment).seqNum) != rcvAck) {
                            (client -> sendQueue) = 0; // Stoppiamo il send durante la trasmissione della coda
                            
                            head = (client -> queueHead);
                            tmpPos = 0;
                            while(atoi((client -> sndWindow)[tmpPos++].seqNum) != rcvAck);
                            (client -> queueHead) = newSegQueue((client -> sndWindow)[tmpPos-1], tmpPos-1);
                            (client -> queueHead) -> next = head;
                            (client -> sendQueue) = 1; // Riavviamo il send durante la trasmissione della coda
                        }
                        // pthread_rwlock_unlock(&(client -> slideLock));
                        if(pthread_rwlock_unlock(&(client -> slideLock)) != 0) {
                            printf("%d : %s\n", errno, strerror(errno));
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
                (client -> sendQueue) = 0; // Stoppiamo il send durante la trasmissione della coda
                pthread_rwlock_wrlock(&(client -> slideLock));
                //printf(" - Ack valido\n");

                /* Calcolo della dimensione dello slide da effettuare */
                slideSize = normalize(rcvAck, atoi((client -> sndWindow)[0].seqNum));
                printf("slide: %d, rcvAck: %d, seqNum: %d\n", slideSize, rcvAck, atoi((client -> sndWindow)[0].seqNum));
                lastAck = rcvAck;
                countAck = 1;

                /* Eliminazione dalla coda di tutti i segmenti più vecchi rispetto all'ack appena ricevuto */
                int maxSeqNumSendable = (atoi((client -> sndWindow)[0].seqNum) + WIN_SIZE-2)%(maxSeqNum) + 1;
                int distance = normalizeDistance(maxSeqNumSendable, rcvAck);
                distance = distance >= WIN_SIZE ? WIN_SIZE-1 : distance;
                // printf("Slide: %d\n", slideSize);
                SegQueue *current = (client -> queueHead);
                SegQueue *prev = (client -> queueHead);
                while(current != NULL) {
                	prev = current;
                	current = current -> next;
                	if(isSeqMinor(rcvAck, atoi((prev -> segment).seqNum), distance)) {
                        deleteSegFromQueue(&(client -> queueHead), prev);
                    }
                    else
                        prev -> winPos -= slideSize;
                }

                /* Se lo slide da effettuare è totale allora resetta tutte le strutture dati flag */  
                if(slideSize == WIN_SIZE) {
                    memset(&(client -> segToSend)[0], 0, sizeof(int)*slideSize);
                    memset(&(client -> segSent)[0], 0, sizeof(int)*slideSize);
                    memset(&(client -> rttFlag)[0], 0, sizeof(int)*slideSize);                    
                }
                else {
                    /* Slide di tutte le strutture dati */
                    // Crashava qui, il comando LIST
                    memmove((client -> segToSend), &(client -> segToSend)[slideSize], sizeof(int)*(WIN_SIZE-slideSize));
                    memset(&(client -> segToSend)[WIN_SIZE-slideSize], 0, sizeof(int)*slideSize);

                    memmove((client -> segSent), &(client -> segSent)[slideSize], sizeof(int)*(WIN_SIZE-slideSize));
                    memset(&(client -> segSent)[WIN_SIZE-slideSize], 0, sizeof(int)*slideSize);

                    memmove((client -> rttFlag), &(client -> rttFlag)[slideSize], sizeof(int)*(WIN_SIZE-slideSize));
                    memset(&(client -> rttFlag)[WIN_SIZE-slideSize], 0, sizeof(int)*slideSize);

                    memmove((client -> segTimeout), &(client -> segTimeout)[slideSize], sizeof(struct timeval)*(WIN_SIZE-slideSize));

                    memmove((client -> segRtt), &(client -> segRtt)[slideSize], sizeof(struct timeval)*(WIN_SIZE-slideSize));

                    memmove((client -> sndWindow), &(client -> sndWindow)[slideSize], sizeof(Segment)*(WIN_SIZE-slideSize));                    
                }

                /* Se si era arrivati all'invio di un pacchetto in posizione X
                   ed è stato fatto uno slide di Y allora riprendi il caricamento/invio 
                   dalla posizione X-Y */
                (client -> sndWinPos) -= slideSize;
                if((client -> sndPos) != 0){
                	(client -> sndPos) -= slideSize;
                } else {
                	(client -> sndPos) = (client -> sndWinPos);
                }

                (client -> sendQueue) = 1; // Riavviamo il send durante la trasmissione della coda

                // pthread_rwlock_unlock(&(client -> slideLock));
                if(pthread_rwlock_unlock(&(client -> slideLock)) != 0) {
                    printf("%d : %s\n", errno, strerror(errno));
                    exit(-1);
                }
            }
        }
        //else printf(" - ACK IGNORATO\n");
    }
}

/* Funzione per la chiusura della connessione */
void fin(ClientNode *client) {

    int ackNum;
    int *tmpIntBuff;
    int countRetransmission = 1;

    Segment *sndSegment = NULL;

    Segment *rcvSegment = (Segment*) malloc(sizeof(Segment));
    if(rcvSegment == NULL)
    {
        printf("Error while trying to \"malloc\" a new segment for FIN operation!\nClosing...\n");
        exit(-1);
    }

    /* Ricezione FIN */
    //recvSegment(client -> sockfd, rcvSegment, &(client -> connection), &(client -> addrlenSocket));

    /* Invio ACK del FIN */
    ackNum = atoi(rcvSegment -> seqNum)%maxSeqNum + 1;
    tmpIntBuff = strToInt(EMPTY);
    newSegment(&sndSegment, FALSE, 1, ackNum, FALSE, TRUE, FALSE, EMPTY, 1, tmpIntBuff); // RFC 1337 -> https://tools.ietf.org/html/rfc1337
    // sendto(client -> sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&(client -> connection), (client -> addrlenSocket));
    randomSendTo(client -> sockfd, sndSegment, (struct sockaddr*)&(client -> connection), (client -> addrlenSocket), loss_prob);

    /* Invio FIN */
    newSegment(&sndSegment, FALSE, 1, ackNum, FALSE, TRUE, TRUE, EMPTY, 1, tmpIntBuff);
    free(tmpIntBuff);
    
    struct timeval finTimeout;
    finTimeout.tv_usec = 0;
    /* Fase di fin 2-way da parte del server */
    do {
        if(atoi(rcvSegment -> finBit) == 1) {
            fin(client);

            free(sndSegment);
            free(rcvSegment);
            return;
        }
        /* Tenta la ritrasmissione al massimo 10 volte incrementando di volta in volta il timeout */
        switch(countRetransmission) {
            case 1:
                finTimeout.tv_sec = 1;
                break;

            case 2:
                finTimeout.tv_sec = 3;
                break;

            case 11:
                finTimeout.tv_sec = 0;  // Reset del timeout a 0 (= attendi all'infinito)

                if (setsockopt(client -> sockfd, SOL_SOCKET, SO_RCVTIMEO, &finTimeout, sizeof(finTimeout)) < 0) {
                    printf("\nError while setting fin-timeout at try n°: %d\n", countRetransmission);
                    exit(-1);
                }

                free(sndSegment);
                free(rcvSegment);

                printf("\nTrasmissione terminata e disconnessione effettuata con successo!\n");

                return;

                break;

            default:
                finTimeout.tv_sec = 7;
                break;
        }

        if (setsockopt(client -> sockfd, SOL_SOCKET, SO_RCVTIMEO, &finTimeout, sizeof(finTimeout)) < 0) {
            printf("\nError while setting fin-timeout at try n°: %d\n", countRetransmission);
            exit(-1);
        }

        // printf("\n[FIN] -> Attempt n°%d...\n", countRetransmission);

        // sendto(client -> sockfd, sndSegment, sizeof(Segment), 0, (struct sockaddr*)&(client -> connection), (client -> addrlenSocket));
        // printf("\n[FIN]: Sent to the client (%s:%d)\n", inet_ntoa((client -> connection).sin_addr), ntohs((client -> connection).sin_port));
        // /* Invio FIN */
        if(randomSendTo(client -> sockfd, sndSegment, (struct sockaddr*)&(client -> connection), (client -> addrlenSocket), loss_prob)){
            // printf("\n[FIN]: Sent to the client (%s:%d)\n", client -> ip, client -> clientPort);
        }
        else{
            // printf("\n[FIN]: LOST\n");
        }

        countRetransmission += 1;

        /* Ricezione FIN-ACK */
    } while(recvSegment(client -> sockfd, rcvSegment, &(client -> connection), &(client -> addrlenSocket)) < 0 || atoi(rcvSegment -> finBit) == 1);

    finTimeout.tv_sec = 0;
    if (setsockopt(client -> sockfd, SOL_SOCKET, SO_RCVTIMEO, &finTimeout, sizeof(finTimeout)) < 0) {
        printf("\nError while setting fin-timeout at try n°: %d\n", countRetransmission);
        exit(-1);
    }

    // printf("Ricevuto qualcosa:\n seqNum: %s\n ackNum: %s\n eotBit: %s\n synBit: %s\n ackBit: %s\n finBit: %s\n", 
    //     rcvSegment -> seqNum, rcvSegment -> ackNum, rcvSegment -> eotBit, rcvSegment -> synBit, rcvSegment -> ackBit, rcvSegment -> finBit);

    free(sndSegment);
    free(rcvSegment);

    printf("\nTrasmissione terminata e disconnessione effettuata con successo!\n");

    return;
}