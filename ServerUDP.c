/* 
    Server - FTP on UDP 

    Authors: Enrico D'Alessandro & Andrea Fortunato
    IIW (A.Y. 2019-2020) at Università di Tor Vergata in Rome.
*/

#include <time.h>
#include "Server.h"

#define IP_PROTOCOL 0

/******************************************************* PROTOTIPI ********************************************************************/
void ctrl_c_handler();                      /* Funzione per la gestione del ctrl_c */
int list(ClientNode *);                     /* Funzione per la gestione del comando list */
int download(ClientNode*, char *);          /* Funzione per la gestione del comando download */
int upload(ClientNode*, Segment*);          /* Funzione per la gestione del comando upload */
void fin(ClientNode *);                     /* Funzione per la gestione della fase di fin */

void *client_thread_handshake(void *);      /* Thread associato al client per la gestione della fase di handshake */
void *timeout_thread(void *);               /* Thread associato al client per la gestione del timeout dei segmenti durante List o Download */
void *continuous_send_thread(void *);       /* Thread associato al client per la gestione dell'invio dei segmenti durante List o Download */
void *continuous_recv_thread(void *);       /* Thread associato al client per la ricezione degli ACK durante List o Download */
void *thread_consumeSegment(void *);        /* Thread associato al client per la consumazione dei segmenti durante l'upload */
/**************************************************************************************************************************************/

/************************************************** VARIABILI GLOBALI *****************************************************************/
int syncFlag;                               
int debug = 0;                              /* Se l'utente ha avviato in modalità debug, verranno mostrate informazioni aggiuntive */

ClientNode *clientList = NULL;
pthread_mutex_t clientListLock;             /* Semaforo Read/Write necessario per la gestione degli accessi alla lista */

FileNode *fileListHead = NULL;              /* Puntatore alla lista di file presenti sul server */
pthread_rwlock_t fileListLock;              /* Semaforo per accesso alla lista dei file presenti su ìl server */
char *fileListStr = NULL;                   /* Lista dei file presenti sul server in formato stringa */

int maxSeqNum;                              /* Massimo numero di sequenza inviabile = WIN_SIZE*2 */
float loss_prob = LOSS_PROB;                /* Variabili utilizzate per il cambio a run-time della probabilità di perdita tramite ctrl_c */
int is_loss_prob_default = 1;
/**************************************************************************************************************************************/


/* Main - Server */ 
int main(int argc, char *argv[]) {

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
    int port = parseCmdLine(argc, argv, "server", NULL, &debug);
    if(port == -1)
    {
        exit(0);
    }

    maxSeqNum = WIN_SIZE*2;

    getFileList(&fileListHead, argv[0]+2);
    fileListStr = fileNameListToString(fileListHead);

    /* Dichiarazione variabili locali Main */
    int mainSockFd;     
    int ret;            
    pthread_t tid;      
    ClientNode *checkClient; 

    /* Sockaddr_in server */
    Sockaddr_in serverSocket;
    bzero(&serverSocket, sizeof(Sockaddr_in));
    serverSocket.sin_family = AF_INET;
    serverSocket.sin_addr.s_addr = htonl(INADDR_ANY);
    serverSocket.sin_port = htons(port);
    //serverSocket.sin_port = htons(47435);
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
           cancello l'handshake_thread precedentemente associato a quel client e lo ricreo */
        pthread_mutex_lock(&clientListLock);
        checkClient = clientList;
        while(checkClient != NULL) {
            if(strcmp(checkClient->ip, inet_ntoa(clientSocket.sin_addr)) == 0 && checkClient->clientPort == ntohs(clientSocket.sin_port)) {
                printf("Client (%s:%d) already connected to the server.\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));
                /* Cancellazione di tutti i thread al client */
                pthread_cancelAndWait(checkClient, checkClient -> handTid);
                pthread_cancelAndWait(checkClient, checkClient -> timeTid);
                pthread_cancelAndWait(checkClient, checkClient -> sendTid);
                pthread_cancelAndWait(checkClient, checkClient -> recvTid);
                pthread_cancelAndWait(checkClient, checkClient -> consTid);

                deleteClientNode(&clientList, checkClient);
                break;
            }
            else
                checkClient = checkClient -> next;
        }

        if(pthread_mutex_unlock(&clientListLock) != 0) {
            printf("%d : %s\n", errno, strerror(errno));
            exit(-1);
        }

        printf("\n[PKT-MAIN]: Client (%s:%d) is trying to connect...\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));
        
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

    /* Distruzione semaforo R/W per la lista di client */
    if(pthread_mutex_destroy(&clientListLock) != 0) {
        printf("Failed semaphore destruction.\n");
        exit(-1);
    }

    /* Distruzione semaforo R/W per la lista di file */
    if(pthread_rwlock_destroy(&fileListLock) != 0) {
        printf("Failed semaphore destruction.\n");
        exit(-1);
    }

    return 0; 
}

/* Funzione per la gestione del segnale ctrl_c */
void ctrl_c_handler() {
    if(is_loss_prob_default) {
        loss_prob = 50;
        is_loss_prob_default = 0;
    } else {
        loss_prob = LOSS_PROB;
        is_loss_prob_default = 1;
    }
    
    printf("New loss_prob: %.2f\n", loss_prob);
}

/* Thread handshake */
void *client_thread_handshake(void *args) {		

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    int clientSockFd;
    int ret;
    int *tmpIntBuff;
    char *tmpStrBuff;

    ThreadArgs threadArgs = *((ThreadArgs*)args);

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

    /* Invio SYN-ACK */
    if(randomSendTo(newClient -> sockfd, synAck, (struct sockaddr*)&(newClient -> connection), (newClient -> addrlenSocket), loss_prob)){
        //printf("SYN-ACK sent to the client (%s:%d)\n", newClient -> ip, newClient -> clientPort);
    }
    else {
        //printf("SYN-ACK LOST\n");
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

    /* Switch operazioni richieste dal client */
    switch(atoi(rcvSegment -> cmdType)) {

        /* Richiesta comando list */
        case 1:
            if(list(newClient) == 0)
                fin(newClient);

            newClient -> mustDie[TIMEOUT] = 1;
            newClient -> mustDie[SEND] = 1;
            pthread_join(newClient -> timeTid, NULL);
            newClient -> timeTid = -1;
            pthread_join(newClient -> sendTid, NULL);
            newClient -> sendTid = -1;

            break;

        /* Richiesta comando download */
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
                newClient -> timeTid = -1;
                pthread_join(newClient -> sendTid, NULL);
                newClient -> sendTid = -1;
            }

            free(tmpStrBuff);
            
            break;

        /* Richiesta comando upload */
        case 3:
            printf("\nInizio fase di upload\n");
            
            ret = upload(newClient, rcvSegment);
            if(ret == 0){
                fin(newClient);
            }
            else 
                pthread_cancelAndWait(newClient, newClient -> consTid);
            
            break;

        default:
            break;
    }

    pthread_mutex_lock(&clientListLock);
    deleteClientNode(&clientList, newClient);
    if(pthread_mutex_unlock(&clientListLock) != 0) {
        printf("%d : %s\n", errno, strerror(errno));
        exit(-1);
    }

    free(rcvSegment);

    pthread_exit(NULL);
}

/* Funzione per la gestione del comando list */
int list(ClientNode *client) {   

    int ret;
    int lenBuff;
    char *fileListStr;
    int buffFile[LEN_MSG];
    int i, j;
    int currentPos = 1; 
    char tmpStrBuff[LEN_MSG];
    int *tmpIntBuff;
    Segment *sndSegment = NULL;

    pthread_rwlock_rdlock(&fileListLock);
    fileListStr = fileNameListToString(fileListHead);
    lenBuff = strlen(fileListStr);
    pthread_rwlock_unlock(&fileListLock);
    
    int totalSegs = (lenBuff-1)/(LEN_MSG) + 1; /* (A-1)/B+1, parte intera superiore della divisione A/B */

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

    /* Caricamento del primo pacchetto contenente \bTOTALSEGS\bNOME\b */  
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

        if(pthread_rwlock_unlock(&(client -> slideLock)) != 0) {
            printf("%d : %s\n", errno, strerror(errno));
            exit(-1);
        }
    }

    printf("[LIST] - Packets all loaded\n");
    free(tmpIntBuff);

    int *joinRet;

    pthread_join(client -> recvTid, (void**)&joinRet);
    
    if(*joinRet == -1)
        return -1;

    return 0;
}

/* Funzione per la gestione del comando download */
int download(ClientNode *client, char *fileName) {   

    pthread_rwlock_rdlock(&fileListLock);
    char *originalFileName = fileExists(fileListHead, fileName);
    pthread_rwlock_unlock(&fileListLock);

    Segment *sndSegment = NULL;
    int *tmpIntBuff;

    /* Se il file è presente nel server */
    if(originalFileName != NULL) {

        int ret;
        int *joinRet;

        /* Apertura del file richiesto dal client */
        FILE *file = fopen(originalFileName, "rb");
        client -> fileDescriptor = file;

        fseek(file, 0, SEEK_END);
        int fileLen = ftell(file);
        fseek(file, 0, SEEK_SET);

        int totalSegs = (fileLen-1)/(LEN_MSG) + 1; /* (A-1)/B+1, parte intera superiore della divisione A/B */

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
        int ch;
        int i, j;
        int currentPos = 1; 

        char tmpStrBuff[LEN_MSG];
        char *fileSHA256 = getFileSHA256(originalFileName);

        /* Caricamento del primo pacchetto contenente \bTOTALSEGS\bNOME\bLUNGHEZZA_IN_BYTE\bCODICE_SHA256 */ 
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

                    if(*((int*)joinRet) == -1)
                        return -1;
                }
            }

            pthread_rwlock_rdlock(&(client -> slideLock));

            (client -> sndWinPos)++;
            (client -> sndWindow)[(client -> sndWinPos)-1] = *sndSegment;
            (client -> segToSend)[(client -> sndWinPos)-1] = 1;

            if(pthread_rwlock_unlock(&(client -> slideLock)) != 0) {
                printf("%d : %s\n", errno, strerror(errno));
                exit(-1);
            }
        }

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
                    FNFTimeout.tv_sec = 0;  /* Reset del timeout a 0 (= attendi all'infinito) */

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

/* Thread timeout per operazioni List & Download */
void *timeout_thread(void *args) {
	
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    ClientNode *client = (ClientNode*) args;

    int timeoutPos = 0;    
    int maxSeqNumSendable;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    /* Alla scadenza di un timeout impostiamo il timestamp del segmento associato
       ad un valore alto così che non venga ricontrollato fino al prossimo
       reinvio */
    tv.tv_sec += 86400; 

    while(1) {

        checkIfMustDie(client, TIMEOUT);

        pthread_rwlock_rdlock(&(client -> slideLock));

        /* Se il segmento sndWindow[timeoutPos] è stato inviato allora controlla il suo timeout */
        if((client -> segSent)[timeoutPos] == 1) {

            /* Se il timeout per il segmento in esame è scaduto */
            if(elapsedTime((client -> segTimeout)[timeoutPos]) > 100) {
                
                pthread_mutex_lock(&(client -> queueLock));

                (client -> segTimeout)[timeoutPos] = tv;

                maxSeqNumSendable = (atoi((client -> sndWindow)[0].seqNum) + WIN_SIZE-2)%(maxSeqNum) + 1;
                orderedInsertSegToQueue(&(client -> queueHead), (client -> sndWindow)[timeoutPos], timeoutPos, maxSeqNumSendable);

                if(pthread_mutex_unlock(&(client -> queueLock)) != 0) {
                    printf("%d : %s\n", errno, strerror(errno));
                    exit(-1);
                }
            }
        }

        timeoutPos = (timeoutPos+1) % WIN_SIZE;

        if(pthread_rwlock_unlock(&(client -> slideLock)) != 0) {
            printf("%d : %s\n", errno, strerror(errno));
            exit(-1);
        }
    }
}

/* Thread per l'invio di segmenti per le di operazioni List & Download */
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

            if(randomSendTo(client -> sockfd, &((client -> queueHead) -> segment), (struct sockaddr*)&(client -> connection), (client -> addrlenSocket), loss_prob) == 1) {
                // printf("\n[RAND_SENDTO-QUEUE] -> pacchetto inviato seqNum: "); //printf("%d\n", atoi(((client -> queueHead) -> segment).seqNum));
            }
            else {
                // printf("\n[RAND_SENDTO-QUEUE] -> pacchetto perso seqNum: "); printf("%d\n", atoi(((client -> queueHead) -> segment).seqNum));
            }

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
            if(pthread_rwlock_unlock(&(client -> slideLock)) != 0) {
                printf("%d : %s\n", errno, strerror(errno));
                exit(-1);
            }
            continue;
        }

        /* Impostiamo il timestamp per il calcolo dell'rtt */
        gettimeofday(&(client -> segRtt)[(client -> sndPos)], NULL);
        
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

        if(pthread_rwlock_unlock(&(client -> slideLock)) != 0) {
            printf("%d : %s\n", errno, strerror(errno));
            exit(-1);
        }
    }
}

/* Thread per la ricezione degli ACK List & Download */
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

        if(recvSegment(client -> sockfd, rcvSegment, &(client -> connection), &(client -> addrlenSocket)) < 0) {
            if(elapsedTime(deadLineTimeout) > 60*1000) {
                printf("RECV FALLITA\n");
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
            *joinRet = 0;
            pthread_exit(joinRet);
        }

        /* Se c'è almeno un segmento caricato processa l'ack */
        if((client -> sndWinPos) != 0) {

            /* Aggiornamento dell'RTO */
            rttPos = normalizeDistance(rcvAckedSeq, atoi((client -> sndWindow)[0].seqNum));
            if(rttPos < WIN_SIZE) {
                if((client -> rttFlag)[rttPos] == 0) {
                    (client -> RTO) = calculateRTO((client -> segRtt)[rttPos], rttData);
                    (client -> rttFlag)[rttPos] = 1;
                }
            }

            /* Calcolo della dimensione dell'eventuale slide da effettuare */
            slideSize = calcSlideSize(rcvAck, atoi((client -> sndWindow)[0].seqNum));

            if(rcvAck == atoi((client -> sndWindow)[0].seqNum) || slideSize > WIN_SIZE) {
                if(rcvAck == lastAck) {
                    /* Meccanismo del Fast Retransmit: aggiunta del segmento in testa alla coda di ritrasmissione */
                    if(++countAck == 3) {
                        pthread_rwlock_wrlock(&(client -> slideLock));
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

                lastAck = rcvAck;
                countAck = 1;

                /* Eliminazione dalla coda di tutti i segmenti più vecchi rispetto all'ack appena ricevuto */
                int maxSeqNumSendable = (atoi((client -> sndWindow)[0].seqNum) + WIN_SIZE-2)%(maxSeqNum) + 1;
                int distance = normalizeDistance(maxSeqNumSendable, rcvAck);
                distance = distance >= WIN_SIZE ? WIN_SIZE-1 : distance;
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

                if(pthread_rwlock_unlock(&(client -> slideLock)) != 0) {
                    printf("%d : %s\n", errno, strerror(errno));
                    exit(-1);
                }
            }
        }
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

    /* Invio ACK del FIN */
    ackNum = atoi(rcvSegment -> seqNum)%maxSeqNum + 1;
    tmpIntBuff = strToInt(EMPTY);
    newSegment(&sndSegment, FALSE, 1, ackNum, FALSE, TRUE, FALSE, EMPTY, 1, tmpIntBuff); // RFC 1337 -> https://tools.ietf.org/html/rfc1337
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
                finTimeout.tv_sec = 0;  /* Reset del timeout a 0 (= attendi all'infinito) */

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

        /* Invio FIN */
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

    free(sndSegment);
    free(rcvSegment);

    printf("\nTransmission terminated and disconnection done successfully!\n");

    return;
}

/* Funzione per la gestione del comando Upload */
int upload(ClientNode *client, Segment *firstRcvSegment) {

    int *tmpIntBuff;
    char *tmpStr; 
    char *tmpStrBuff;
    int lastSeqNumSent;
    int lastSeqNumRecv;
    int slot;
    int firstArrived = 0;
    int totalSegs = 0;
    char fileName[256];
    char originalFileSHA256[65];
    char *currentFileSHA256;

    Segment *sndSegment = NULL;
    Segment *rcvSegment = (Segment*)malloc(sizeof(Segment));
    if(rcvSegment == NULL) {
        printf("Error while trying to \"malloc\" a new download rcvsegment!\nClosing...\n");
        exit(-1);
    } 

    *rcvSegment = *firstRcvSegment;

    /* Timer associato al Upload Request */
    struct timeval rcvTimeout;
    rcvTimeout.tv_usec = 0;
    
    /* Timeout per la ricezione di un segmento: se rimango 64 secondi in attesa, presumibilmente il
       client si è disconnesso */
    rcvTimeout.tv_sec = 64;
    if (setsockopt(client -> sockfd, SOL_SOCKET, SO_RCVTIMEO, &rcvTimeout, sizeof(rcvTimeout)) < 0) {
        printf("\nError while receiving segment...\n");
        exit(-1);
    }

    memset(client -> rcvWinFlag, 0, WIN_SIZE*sizeof(int));

    printf("\nInizio ricezione file...\n");

    /* Ricezione file */  
    while(1) {

        lastSeqNumRecv = atoi(rcvSegment -> seqNum);

        // printf("%d\n", lastSeqNumRecv);

        //printf("\nRicevuto pacchetto - (seqNum: %d)\n", atoi(rcvSegment -> seqNum));

        pthread_mutex_lock(&(client -> consumeLock));

        /* Calcolo slot finestra e memorizzazione segmento ricevuto e aggiornamento rcvWinFlag */
        slot = normalizeDistance(lastSeqNumRecv, client -> lastAckNumSent);
        
        /* Primo pacchetto ricevuto dal server contenente filename e dimensione */
        if(firstArrived == 0 && slot == 0) {

            firstArrived = 1;

            tmpStrBuff = intToStr(rcvSegment -> msg, atoi(rcvSegment -> lenMsg));
            //wprintf(L"\nPACCHETTO INIZIALE RICEVUTO: %s, LEN: %d\n", tmpStrBuff, atoi(rcvSegment -> lenMsg));

            tmpStr = strtok(tmpStrBuff, "\b");
            totalSegs = atoi(tmpStr);
            printf("\nASPETTO %d SEGMENTI...\n", totalSegs);

            tmpStr = strtok(NULL, "\b");
            strcpy(fileName, tmpStr);
            printf("\nNOME ORIGINALE: %s...\n", fileName);

            tmpStr = strtok(NULL, "\b");
            strcpy(originalFileSHA256, tmpStr);
            printf("\nSHA256: %s\n", originalFileSHA256);

            free(tmpStrBuff);

            client -> fileDescriptor = fopen(fileName, "wb");

            (client -> rcvWindow)[0] = *rcvSegment;
            (client -> rcvWinFlag)[0] = 1;
            client -> totalSegsRcv++;

            /* Creazione di un thread per la scrittura dei segmenti su disco durante l'upload */
            if(pthread_create(&(client -> consTid), NULL, thread_consumeSegment, (void*)client) != 0) {
                printf("New consumeSegment thread error\n");
                exit(-1);
            }
        } 
        else {
            /* Se il pacchetto ricevuto è valido e non duplicato, ovvero uno dei pacchetti attesi e non un pacchetto già riscontrato e copiato su disco */
            if(slot < WIN_SIZE && (client -> rcvWinFlag)[slot] == 0) {
                /* Bufferizzo il segmento appena ricevuto */
                (client -> rcvWindow)[slot] = *rcvSegment;
                (client -> rcvWinFlag)[slot] = 1;
                client -> totalSegsRcv++;
            }
        }        

        pthread_mutex_unlock(&(client -> consumeLock));

        if(client -> totalSegsRcv <= totalSegs) {
            /* Se è consumabile aspetto che il consume scriva su file */
            if((client -> rcvWinFlag)[0] == 1) {
                /* Attendi che il consume abbia consumato e aggiornato lastAckNumSent */
                while(client -> canSendAck == 0); 
                client -> canSendAck = 0;
            }
        }
        lastSeqNumSent = lastSeqNumSent%maxSeqNum + 1;

        tmpIntBuff = strToInt(rcvSegment -> seqNum);

        if(client -> totalSegsRcv >= totalSegs) 
            newSegment(&sndSegment, TRUE, lastSeqNumSent, client -> lastAckNumSent, FALSE, TRUE, FALSE, "3", strlen(rcvSegment -> seqNum), tmpIntBuff);
        else
            newSegment(&sndSegment, FALSE, lastSeqNumSent, client -> lastAckNumSent, FALSE, TRUE, FALSE, "3", strlen(rcvSegment -> seqNum), tmpIntBuff);

        randomSendTo(client -> sockfd, sndSegment, (struct sockaddr*)&(client -> connection), client -> addrlenSocket, loss_prob);
        free(tmpIntBuff);

        if(atoi(rcvSegment -> finBit) == 1) {
            pthread_join(client -> consTid, NULL);
            fclose(client -> fileDescriptor);
            client -> fileDescriptor = NULL;
            printf("\nCalculating SHA256...");
            currentFileSHA256 = getFileSHA256(fileName);
            if(strcasecmp(currentFileSHA256, originalFileSHA256) == 0)
                printf("\rUpload completed successfully!\nSHA256: %s\n", currentFileSHA256);
            else
                printf("\rThere was an error, upload gone wrong!\nOriginal SHA256: %s\n Current SHA256: %s\n", originalFileSHA256, currentFileSHA256);

            break;
        }

        if(recvSegment(client -> sockfd, rcvSegment, &(client -> connection), &(client -> addrlenSocket)) < 0) {
            printf("\n[UPLOAD] -> Client dead. UPLOAD FAILED!\n");
            
            if(sndSegment != NULL) 
                free(sndSegment);

            free(rcvSegment);

            return -1;
        }
    }

    free(sndSegment);
    free(rcvSegment);

    /* Aggiunta del file caricato dal client alla lista di file presenti sul server */
    pthread_rwlock_wrlock(&fileListLock);
    int fileNameLen = strlen(fileName);
    fileName[fileNameLen] = '\n';
    addFile(&fileListHead, fileName);
    fileListStr = fileNameListToString(fileListHead);
    pthread_rwlock_unlock(&fileListLock);

    return 0;
}

/* Thread per la scrittura dei segmenti su file per l'operazione Upload */
void *thread_consumeSegment(void *tmpClient) {

    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    int firstArrived = 0;
    int slot;
    char *tmpStrBuff;
    ClientNode *client = (ClientNode*)tmpClient;

    while(1) {

        /* Rimani fermo finchè il segmento atteso non è consumabile */
        while((client -> rcvWinFlag)[0] == 0);
        pthread_mutex_lock(&(client -> consumeLock));

        if(firstArrived == 0) {

            firstArrived = 1;

            client -> lastAckNumSent = 2; 

            memmove(client -> rcvWindow, &(client -> rcvWindow)[1], sizeof(Segment)*(WIN_SIZE-1));
            memmove(client -> rcvWinFlag, &(client -> rcvWinFlag)[1], sizeof(int)*(WIN_SIZE-1));
            memset(&(client -> rcvWinFlag)[WIN_SIZE-1], 0, sizeof(int)*1);

            /* Sblocco recv_thread per invio ack */
            client -> canSendAck = 1; 

            pthread_mutex_unlock(&(client -> consumeLock));
            continue;
        }

        slot = 0;

        /* Consuma tutti i pacchetti in ordine fino al primo non disponibile */
        do {

            tmpStrBuff = intToStr((client -> rcvWindow)[slot].msg, atoi((client -> rcvWindow)[slot].lenMsg));
            cpOnFile(client -> fileDescriptor, tmpStrBuff, atoi((client -> rcvWindow)[slot].lenMsg));
            free(tmpStrBuff);

            /* Reimposta lo slot di rcvWinFlag come slot libero per un nuovo pacchetto */
            (client -> rcvWinFlag)[slot] = 0;

            if(atoi((client -> rcvWindow)[slot].eotBit) == 1) {

                printf("\nUltimo pacchetto ricevuto termino.\n");

                /* AckNum da inviare al server */
                client -> lastAckNumSent = atoi((client -> rcvWindow)[slot].seqNum)%maxSeqNum + 1;

                /* Sblocco upload per invio ack */
                client -> canSendAck = 1;
                client -> totalSegsRcv++;

                memset(client -> rcvWinFlag, 0, sizeof(int)*WIN_SIZE);

                pthread_mutex_unlock(&(client -> consumeLock));

                pthread_exit(0);
            }

            slot++;

        } while((client -> rcvWinFlag)[slot] == 1 && slot < WIN_SIZE);        

        /* AckNum da inviare al server */
        client -> lastAckNumSent = atoi((client -> rcvWindow)[slot-1].seqNum)%maxSeqNum + 1;

        /* Slide delle strutture di ricezione */
        if(slot == WIN_SIZE) {
            memset(client -> rcvWinFlag, 0, sizeof(int)*WIN_SIZE);
        } else {
            memmove(client -> rcvWindow, &(client -> rcvWindow)[slot], sizeof(Segment)*(WIN_SIZE-slot));
            memmove(client -> rcvWinFlag, &(client -> rcvWinFlag)[slot], sizeof(int)*(WIN_SIZE-slot));
            memset(&(client -> rcvWinFlag)[WIN_SIZE-slot], 0, sizeof(int)*slot);
        }

        /* Sblocco recv_thread per invio ack */
        client -> canSendAck = 1; 

        pthread_mutex_unlock(&(client -> consumeLock));
    }
} 