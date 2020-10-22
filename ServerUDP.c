// https://packetlife.net/blog/2010/jun/7/understanding-tcp-sequence-acknowledgment-numbers/

// Server - FTP on UDP 
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include "Server.h"

#define IP_PROTOCOL 0
#define PORT 5555
#define NOFILE "File Not Found!"

/* Prototipi */
void *client_thread_handshake(void *);                /* Thread associato al client */
void ctrl_c_handler();

/* Variabili globali */
pthread_rwlock_t lockList;                  /* Semaforo Read/Write necessario per la gestione degli accessi alla lista */
int syncFlag = 0;

int len;

ClientNode *clientList = NULL;
int clientListSize = 0;
int maxSockFd = 0;

// Main - Server 
int main(int argc, char *argv[])
{ 
    system("clear");
    setbuf(stdout,NULL);
    srand(time(0));
    signal(SIGINT, ctrl_c_handler);

    /* Inizializzazione semaforo R/W */
    if(pthread_rwlock_init(&lockList, NULL) != 0) {
        printf("Failed semaphore initialization.\n");
        exit(-1);
    }

    int mainSockFd;
    int ret;
    int exist = 0;

    pthread_t tid;

    ClientNode *client;

    /* Struct sockaddr_in server */
    struct sockaddr_in serverSocket;
    serverSocket.sin_family = AF_INET;
    serverSocket.sin_port = htons(PORT);
    serverSocket.sin_addr.s_addr = htonl(INADDR_ANY);
    int addrlenServer = sizeof(serverSocket);

    /* Struct sockaddr_in client */
    struct sockaddr_in clientSocket;
    int addrlenClient = sizeof(clientSocket);

    /* Struct Segment di ricezione */
    Segment *rcvSegment = (Segment*)malloc(sizeof(Segment));
	if(rcvSegment == NULL)
	{
		printf("Error while trying to \"malloc\" a new Segment!\nClosing...\n");
		exit(-1);
	} 
	bzero(rcvSegment, sizeof(Segment));

	/*  */
    ThreadArgs *threadArgs = (ThreadArgs *)malloc(sizeof(ThreadArgs));
	if(threadArgs == NULL)
	{
		printf("Error while trying to \"malloc\" a new ThreadArgs!\nClosing...\n");
		exit(-1);
	}
	bzero(threadArgs, sizeof(ThreadArgs));

    /* Socket - UDP */
    mainSockFd = socket(AF_INET, SOCK_DGRAM, IP_PROTOCOL);
    if (mainSockFd < 0){
        printf("\nFile descriptor not received!\n");
        exit(-1);
    }
    else
        printf("\nFile descriptor %d received!\n", mainSockFd);

    /* Assegnazione dell'indirizzo locale alla socket */
    if (bind(mainSockFd, (struct sockaddr*)&serverSocket, addrlenServer) == 0)
        printf("\nSuccessfully binded!\n");
    else {
        printf("\nBinding Failed!\n");
        exit(-1);
    }
	
	/* Blocco semi funzionante */
    // /* Server in attesa di richieste da parte dei client */
    // while(recvfrom(mainSockFd, rcvSegment, sizeof(Segment), 0, (struct sockaddr*)&clientSocket, (socklen_t*)&addrlenClient) > 0) {

    //     printf("\nWaiting for operation request... %s:%d\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));

    //     if((strlen(rcvSegment -> eotBit) == 0) || (strlen(rcvSegment -> seqNum) == 0) || 
    //        (strlen(rcvSegment -> ackNum) == 0) || (strlen(rcvSegment -> synBit) == 0) || 
    //        (strlen(rcvSegment -> ackBit) == 0) || (strlen(rcvSegment -> finBit) == 0) || 
    //        (strlen(rcvSegment -> winSize) == 0) || (strlen(rcvSegment -> cmdType) == 0) ||
    //        (strlen(rcvSegment -> msg) == 0)) 
    //     {
    //     	printf("\n[Error]: Empty segment received from %s:%d\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));
    //     }
    //     else {
    //     	/* Controllo se il Client esiste: in caso affermativo esegui l'operazione richiesta altrimenti utilizza un thread per la fase di 3-way handshake */
	   //      client = clientList;
	   //      exist = 0;
	   //      while(client != NULL) {
	   //          if(client -> clientPort == ntohs(clientSocket.sin_port)) {
	   //              // Client esiste
	   //              exist = 1;
	   //              break;
	   //          }
	   //          client = client -> next;
	   //      }

	   //      if(exist) {
	   //          /* Controlliamo se rcvSegment è un segmento valido */
	   //      	if(((atoi(rcvSegment -> ackBit) != 1) || (atoi(rcvSegment -> ackNum) != (client -> lastSeqServer + 1))) && (strlen(rcvSegment -> msg) == 0)) {
	   //      		printf("Error: wrong rcvSegment received from %s:%d\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));
	   //      		exit(-1);
	   //  		}

	   //  		 Controlliamo se è un ACK+messaggio del SYN-ACK 
	   //  		if((atoi(rcvSegment -> ackBit) == 1) && (atoi(rcvSegment -> ackNum) == (client -> lastSeqServer + 1)) && (strlen(rcvSegment -> msg) != 0)) {
	   //      		/* Terminazione thread handshake */
	   //  			pthread_cancel(client -> tid);
				// 	pthread_join(client -> tid, NULL);
	   //  		}
	    		
	   //  		/* Switch operazione richiesta */
	   //  		switch(atoi(rcvSegment -> cmdType)) {

	   //  			/* list */
	   //  			case 1:
	   //  				break;

	   //  			/* download */
	   //  			case 2:
	   //  				break;

	   //  			/* upload */
	   //  			case 3:
	   //  				break;

	   //  			/* Errore */
	   //  			default:
	   //  				printf("Error: wrong rcvSegment received from %s:%d\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));
	   //      			exit(-1);
	   //  				break;
	   //  		}
	    		
	   //      } 
	   //      else {
	   //      	threadArgs = newThreadArgs(clientSocket, rcvSegment -> seqNum);

	   //          /* Creazione di un thread utile alla fase di handshake */
	   //          ret = pthread_create(&tid, NULL, client_thread_handshake, (void *)threadArgs);
	   //          if(ret != 0)
	   //          {
	   //              printf("New client thread error\n");
	   //              exit(-1);
	   //          }
	   //      }

	   //      //printf("\n\nReceived packet from %d:%s:%d\n", newClient->sockfd, newClient->ip, newClient->clientPort);
	   //      //printf("\nClient says: %s", net_buf);

	   //      while(syncFlag == 0);
	   //      pthread_rwlock_rdlock(&lockList);
	   //      printf("\n\n---> STAMPA LISTA <---\n\n");
	   //      printf("|Size: %d - MAx: %d|\n", clientListSize, maxSockFd);
	   //      printList(clientList);
	   //      pthread_rwlock_unlock(&lockList);
	   //      syncFlag = 0;
    //     }
        
    //     /* Azzeramento strutture dati di ricezione */
	   //  bzero(rcvSegment, sizeof(Segment));
    // } 

    /* Server in attesa di richieste da parte dei client */
    while(1) {

        ret = recvfrom(mainSockFd, rcvSegment, sizeof(Segment), 0, (struct sockaddr*)&clientSocket, (socklen_t*)&addrlenClient);
        printf("\n[MAIN_THREAD_RECV]: Return value %d\n", ret);

        if((strlen(rcvSegment -> eotBit) == 0) || (strlen(rcvSegment -> seqNum) == 0) || 
           (strlen(rcvSegment -> ackNum) == 0) || (strlen(rcvSegment -> synBit) == 0) || 
           (strlen(rcvSegment -> ackBit) == 0) || (strlen(rcvSegment -> finBit) == 0) || 
           (strlen(rcvSegment -> winSize) == 0) || (strlen(rcvSegment -> cmdType) == 0) ||
           (strlen(rcvSegment -> msg) == 0)) 
        {
        	printf("\n[Error]: Empty segment received from (%s:%d)\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));
        }
        else {
        	/* Controllo se il Client esiste: in caso affermativo esegui l'operazione richiesta altrimenti utilizza un thread per la fase di 3-way handshake */
	        client = clientList;
	        exist = 0;
	        while(client != NULL) {
	            if(client -> clientPort == ntohs(clientSocket.sin_port)) {
	                // Client esiste
	                exist = 1;
	                break;
	            }
	            client = client -> next;
	        }

	        if(exist) {
	            /* Controlliamo se rcvSegment è un segmento valido */
	        	if(((atoi(rcvSegment -> ackBit) != 1) || (atoi(rcvSegment -> ackNum) != (client -> lastSeqServer + 1))) && (strlen(rcvSegment -> msg) == 0)) {
	        		printf("Error: wrong rcvSegment received from (%s:%d)\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));
	        		exit(-1);
	    		}

	    		/* Controlliamo se è un ACK+messaggio del SYN-ACK */
	    		if((atoi(rcvSegment -> ackBit) == 1) && (atoi(rcvSegment -> ackNum) == (client -> lastSeqServer + 1)) && (strlen(rcvSegment -> msg) != 0)) {
	        		/* Terminazione thread handshake */
	    			pthread_cancel(client -> tid);
					pthread_join(client -> tid, NULL);
	    		}
	    		
	    		/* Switch operazione richiesta */
	    		switch(atoi(rcvSegment -> cmdType)) {

	    			/* list */
	    			case 1:
	    				break;

	    			/* download */
	    			case 2:
	    				break;

	    			/* upload */
	    			case 3:
	    				break;

	    			/* Errore */
	    			default:
	    				printf("Error: wrong rcvSegment received from (%s:%d)\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));
	        			exit(-1);
	    				break;
	    		}
	    		
	        } 
	        else {
	        	threadArgs = newThreadArgs(clientSocket, rcvSegment -> seqNum);
	        	
	        	printf("\n[PKT]: Received packet from (%s:%d)\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));
	        	printf("[MSG]: %s\n", rcvSegment -> msg);

	        	// Segment *synAck = mallocSegment("0", "1", TRUE, TRUE, FALSE, "1", "SYN-ACK\0");
	        	// sendto(mainSockFd, synAck, sizeof(Segment), 0, (struct sockaddr*)&(clientSocket), addrlenClient);

	        	// recvfrom(mainSockFd, rcvSegment, sizeof(Segment), 0, (struct sockaddr*)&clientSocket, (socklen_t*)&addrlenClient);
	        	// printf("\nHandshake terminated! %s received\n", rcvSegment -> msg);

	            /* Creazione di un thread utile alla fase di handshake */
	            ret = pthread_create(&tid, NULL, client_thread_handshake, (void *)threadArgs);
	            if(ret != 0)
	            {
	                printf("New client thread error\n");
	                exit(-1);
	            }
	        }

	        //printf("\n\nReceived packet from %d:%s:%d\n", newClient->sockfd, newClient->ip, newClient->clientPort);
	        //printf("\nClient says: %s", net_buf);

	        while(syncFlag == 0);
	        pthread_rwlock_rdlock(&lockList);
	        printf("\n\n---> STAMPA LISTA <---\n\n");
	        printf("|Size: %d - Max: %d|\n", clientListSize, maxSockFd);
	        printList(clientList);
	        pthread_rwlock_unlock(&lockList);
	        syncFlag = 0;
        }
        
        /* Azzeramento strutture dati di ricezione */
	    bzero(rcvSegment, sizeof(Segment));
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
    int sock;
    printf("Socket da eliminare: ");
    scanf("%d", &sock);

    ClientNode *tmp = clientList;

    while(tmp != NULL)
    {
        if(tmp -> sockfd == sock)
            deleteClientNode(&clientList, tmp, &clientListSize, &maxSockFd);

        tmp = tmp -> next;
    }

    printf("\n\n---> STAMPA LISTA <---\n\n");
    printf("|Size: %d - Max: %d|\n", clientListSize, maxSockFd);
    printList(clientList);
}

/* Thread associato al client */
void *client_thread_handshake(void *args)
{		
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    int clientSockFd;
    int ret;

    ThreadArgs *threadArgs = (ThreadArgs*)args;
    printf("\n[TEST]: threadArgs for new client (%s:%d)!\n", inet_ntoa(threadArgs -> clientSocket.sin_addr), ntohs(threadArgs -> clientSocket.sin_port));


    /* Struct sockaddr_in server */
    struct sockaddr_in serverSocket;
    serverSocket.sin_family = AF_INET;
    serverSocket.sin_addr.s_addr = inet_addr(inet_ntoa(threadArgs -> clientSocket.sin_addr));
    int addrlenServer = sizeof(serverSocket);

    /* Struct sockaddr_in client */
    struct sockaddr_in clientSocket;
    int addrlenClient = sizeof(addrlenClient);
    clientSocket = threadArgs -> clientSocket;

    /* Socket UDP */
    clientSockFd = socket(AF_INET, SOCK_DGRAM, IP_PROTOCOL);
    if (clientSockFd < 0){
        printf("\nFailed creating socket for new client (%s:%d)!\n", inet_ntoa(threadArgs -> clientSocket.sin_addr), ntohs(threadArgs -> clientSocket.sin_port));
        exit(-1);
    }

    do {
        serverSocket.sin_port = htons(1024 + rand() % (65535+1 - 1024));
    } while(bind(clientSockFd, (struct sockaddr*)&serverSocket, addrlenServer) != 0);

    ClientNode *newClient = newNode(clientSockFd, inet_ntoa(threadArgs -> clientSocket.sin_addr), ntohs(threadArgs -> clientSocket.sin_port), pthread_self(), ntohs(serverSocket.sin_port), threadArgs -> seqNumClient);
    
    pthread_rwlock_wrlock(&lockList);
    syncFlag = 1;
    addClientNode(&clientList, newClient, &clientListSize, &maxSockFd);
    pthread_rwlock_unlock(&lockList);

    printf("\n\nThread created for (%s:%d), bind on %d\n", newClient -> ip, newClient -> clientPort, newClient -> serverPort);

    /* SYN-ACK */
    char ackNum[MAX_SEQ_ACK_NUM];
    sprintf(ackNum, "%d", atoi(threadArgs -> seqNumClient) + 1);

    Segment *synAck = mallocSegment("0", ackNum, TRUE, TRUE, FALSE, "1", "SYN-ACK\0");

    /* Invio SYN-ACK */
    if((ret = sendto(clientSockFd, synAck, sizeof(Segment), 0, (struct sockaddr*)&(threadArgs -> clientSocket), addrlenClient)) != sizeof(Segment)) {
        printf("Error trying to send a SYN-ACK segment to client (%s:%d)\n", inet_ntoa(threadArgs -> clientSocket.sin_addr), ntohs(threadArgs -> clientSocket.sin_port));
        //exit(-1);
    }
	printf("SYN-ACK sent to the client (%s:%d)\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));
	newClient -> lastSeqServer = 0;

    /* ACK del SYN-ACK */
    Segment *rcvSegment = (Segment*) malloc(sizeof(Segment));
    if(rcvSegment == NULL)
    {
        printf("Error while trying to \"malloc\" a new ACK segment of SYN-ACK!\nClosing...\n");
        exit(-1);
    }
    
    while(recvfrom(clientSockFd, rcvSegment, sizeof(Segment), 0, (struct sockaddr*)&clientSocket, (socklen_t*)&addrlenClient) > 0) {
		if((strlen(rcvSegment -> eotBit) == 0) || (strlen(rcvSegment -> seqNum) == 0) || 
           (strlen(rcvSegment -> ackNum) == 0) || (strlen(rcvSegment -> synBit) == 0) || 
           (strlen(rcvSegment -> ackBit) == 0) || (strlen(rcvSegment -> finBit) == 0) || 
           (strlen(rcvSegment -> winSize) == 0) || (strlen(rcvSegment -> cmdType) == 0) ||
           (strlen(rcvSegment -> msg) == 0)) 
        {
        	printf("\n[Error]: Empty segment received from server\n");
        }
        else {
        	break;	
        }		
	}

    if((atoi(rcvSegment -> ackBit) != 1) || (atoi(rcvSegment -> ackNum) != (newClient -> lastSeqServer + 1))) {
        printf("Error: wrong ACK of SYN-ACK received from (%s:%d)\n", inet_ntoa(threadArgs -> clientSocket.sin_addr), ntohs(threadArgs -> clientSocket.sin_port));
        exit(-1);
    }

    printf("\n[PKT_HANDSHAKE_THREAD]: Received packet from (%s:%d)\n", inet_ntoa(clientSocket.sin_addr), ntohs(clientSocket.sin_port));
	printf("[MSG_HANDSHAKE_THREAD]: %s\n", rcvSegment -> msg);
    
    newClient -> lastSeqClient = atoi(rcvSegment -> seqNum);

    printf("\nHandshake terminated with client (%s:%d)\n", inet_ntoa(threadArgs -> clientSocket.sin_addr), ntohs(threadArgs -> clientSocket.sin_port));

    close(clientSockFd);
    pthread_exit(NULL);

    /* 
	 *
	 *
	 * Timeout syn-ack: se dopo X secondi/minuti non ho ricevuto 
	 *
	 *
    */
    

}