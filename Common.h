/* 
    Library for the server.c and client.c- FTP on UDP 

    Authors: Enrico D'Alessandro & Andrea Fortunato
    IIW (A.Y. 2019-2020) at Università di Tor Vergata in Rome.
*/

#ifndef COMMON_H
#define COMMON_H

#include <pthread.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>

#define bzero(buffer,lenght) (memset((buffer), '\0', (lenght)), (void) 0)

#define LOSS_PROB 0 		/* Probabiltà di perdita */
#define WIN_SIZE 10			/* Dimensione finestra */

#define BIT 2
#define CMD 2
#define WIN 11				/* Numero di cifre incluso \0 rappresentanti la dimensione della finestra */
#define BYTE_MSG 5			/* Numero di cifre incluso \0 della lunghezza del messaggio LEN_MSG */
#define LEN_MSG 4096		/* Lunghezza messaggio */

#define TRUE "1"
#define FALSE "2"
#define EMPTY " "

enum threads {
	HANDSHAKE=0,
	TIMEOUT=1,
	SEND=2,
	RECV=3
};

typedef struct sockaddr_in Sockaddr_in;

/* Struttura dati associata al Segmento */
typedef struct _Segment {
	char eotBit[BIT];				/* End Of Transmission Bit: posto ad 1 se l'operazione (download/upload/list)
									   è conclusa con successo, 0 altrimenti */
	char seqNum[WIN];				/* Numero di sequenza del client/server */
	char ackNum[WIN];				/* Ack del segmento ricevuto */
	
	char synBit[BIT];          		/* Bit di syn */
	char ackBit[BIT];				/* Bit di ack */
	char finBit[BIT];				/* Bit di fin */

	char winSize[WIN];				/* Dimensione finestra */

	char cmdType[CMD];				/* Operazione richiesta */
	char lenMsg[BYTE_MSG];			/* Lunghezza, in byte, del campo msg */
	int msg[LEN_MSG];				/* Contenuto del messaggio */
} Segment;

/* Struttura dati associata alla coda di segmenti da reinviare */
typedef struct _SegQueue {
	Segment segment;
	int winPos;

	struct _SegQueue *next;
} SegQueue;

/* Struttura dati associata al calcolo dell'RTO */
typedef struct _RTT_Data {
	double sRTT;
	double varRTT;
} RTT_Data;

void newSegment(Segment **segment, char *eotBit, int seqNum, int ackNum, char *synBit, char *ackBit, char *finBit, char *cmdType, int lenMsg, int *msg);
Segment* mallocSegment(char *eotBit, int seqNum, int ackNum, char *synBit, char *ackBit, char *finBit, char *cmdType, int lenMsg, int *msg);

/* ***************************************************************************************** */

/* Inizializzazione di un nuovo client */
void newSegment(Segment **segment, char *eotBit, int seqNum, int ackNum, char *synBit, char *ackBit, char *finBit, char *cmdType, int lenMsg, int *msg) {

	if(*segment == NULL) {
		*segment = mallocSegment(eotBit, seqNum, ackNum, synBit, ackBit, finBit, cmdType, lenMsg, msg);
	}

	int i;
	char tmpBuff[LEN_MSG];

	bzero((*segment), sizeof(Segment));

	strcpy((*segment) -> eotBit, eotBit);

	sprintf(tmpBuff, "%d", seqNum);
	strcpy((*segment) -> seqNum, tmpBuff);
	bzero(tmpBuff, LEN_MSG);
	if(ackNum != -1) {
		sprintf(tmpBuff, "%d", ackNum);
		strcpy((*segment) -> ackNum, tmpBuff);
		bzero(tmpBuff, LEN_MSG);
	} else {
		strcpy((*segment) -> ackNum, EMPTY);
	}

	strcpy((*segment) -> synBit, synBit);
	strcpy((*segment) -> ackBit, ackBit);
	strcpy((*segment) -> finBit, finBit);
	
	strcpy((*segment) -> winSize, "5");
	
	strcpy((*segment) -> cmdType, cmdType);
	sprintf(tmpBuff, "%d", lenMsg);
	strcpy((*segment) -> lenMsg, tmpBuff);

	for(i = 0; i < lenMsg; i++) 
		((*segment) -> msg)[i] = msg[i];

}

/* Inizializzazione di un nuovo client */
Segment* mallocSegment(char *eotBit, int seqNum, int ackNum, char *synBit, char *ackBit, char *finBit, char *cmdType, int lenMsg, int *msg) {
	Segment *segment = (Segment*) malloc(sizeof(Segment));
	if(segment != NULL)
	{
		newSegment(&segment, eotBit, seqNum, ackNum, synBit, ackBit, finBit, cmdType, lenMsg, msg);
	} else {
		printf("Error while trying to \"malloc\" a new Segment!\nClosing...\n");
		exit(-1);
	}

	return segment;
}

/* Creazione di un nuovo nodo SegQueue */
SegQueue* newSegQueue(Segment segment, int winPos) {
	SegQueue *segQueue = malloc(sizeof(SegQueue));
	if(segQueue != NULL) {
		segQueue -> segment = segment;
		segQueue -> winPos = winPos;

		segQueue -> next = NULL;
		// segQueue -> prev = NULL;
	} else {
		printf("Error while trying to \"malloc\" a new segQueue (SeqNum: %d)!\nClosing...\n", atoi(segment.seqNum));
		exit(-1);
	}

	return segQueue;
}

/* Aggiunta di un SegQueue in coda della coda di ritrasmissione */
void appendSegToQueue(SegQueue **queueHead, Segment segment, int winPos) {
	if(*queueHead == NULL) {
		*queueHead = newSegQueue(segment, winPos);
	} else {
		SegQueue *current = *queueHead;

		while(current -> next != NULL) {
			if((current -> segment).seqNum == segment.seqNum)
				return;
			current = current -> next;
		}

		current -> next = newSegQueue(segment, winPos);
	}
}

/* Eliminazione di un SegQueue dalla coda di ritrasmissione */
void deleteSegFromQueue(SegQueue **queueHead, SegQueue *segment) {
	SegQueue *current, *prev;
	current = *queueHead;

	/* Se segment è in testa */
	if(current == segment) {
		/* Se è l'unico elemento della coda */
		if(current -> next == NULL) {
			free(segment);
			*queueHead = NULL;
		} else {
			prev = current;
			*queueHead = current -> next;
			free(prev);
		}
	} else {
		prev = current;
		while(current != segment) {
			prev = current;
			current = current -> next;
		}

		prev -> next = current -> next;
		free(current);
	}
}

/* Inizializzazione parametri per il calcolo dll'RTO */
RTT_Data* initData() {

	RTT_Data *rttData = malloc(sizeof(RTT_Data));
	if(rttData != NULL) {
		rttData -> sRTT = -1;
		rttData -> varRTT = 0;
	} else {
		printf("Error while trying to \"malloc\" a new rttData!\nClosing...\n");
		exit(-1);
	}

	return rttData;
} 

/* Conversione intera stringa in minuscolo */
char *tolowerString(char *string)
{
	int i = 0;
	
	char *tmp = malloc(strlen(string));
	if(tmp == NULL) {
		printf("Error while trying to \"malloc\" a new toLowerString!\nClosing...\n");
		exit(-1);
	}
	bzero(tmp, strlen(string));

	strcpy(tmp, string);
	while((tmp[i] = tolower(tmp[i])))
		i++;

	return tmp;
}

/* Controllo dei parametri iniziali */
int parseCmdLine(int argc, char **argv, char *who, char **ip, int *debug)
{
	int port;

	/* Il chiamante è il server */
	if(strcmp(who, "server") == 0)
	{
		switch(argc)
		{
			/* Help */
			case 2:
				if(!strcmp(tolowerString(argv[1]), "-h") || !strcmp(tolowerString(argv[1]), "-help") || !strcmp(tolowerString(argv[1]), "--h") || !strcmp(tolowerString(argv[1]), "--help"))
					printf("Syntax:\n\t %s -p PORT_NUMBER [-d, -debug]\n", argv[0]);
				else
					printf("For more information run: \n\t\033[2;3m%s -h\033[0m\n", argv[0]);
				return -1;

				break;

			/* Caso 'corretto' */
			case 3:
				port = atoi(argv[2]);
				if(strcmp(tolowerString(argv[1]), "-p"))
				{
					printf("Syntax:\n\t %s -p PORT_NUMBER [-d, -debug]\n", argv[0]);
					return -1;
				}
				else if((port < 49152 || port > 65535) && port != 47435)
				{
					printf("PORT_NUMBER must be a number between 1024 and 65535\n");
					return -1;
				}
				else
					return port;

				break;

			case 4:
				port = atoi(argv[2]);
				if(strcmp(tolowerString(argv[1]), "-p"))
				{
					printf("Syntax:\n\t %s -p PORT_NUMBER [-d, -debug]\n", argv[0]);
					return -1;
				}
				else if((port < 49152 || port > 65535) && port != 47435)
				{
					printf("PORT_NUMBER must be a number between 1024 and 65535\n");
					return -1;
				}
				else
				{
					if(!strcmp(tolowerString(argv[3]), "-d") || !strcmp(tolowerString(argv[3]), "-debug"))
						*debug = 1;
					else
					{
						printf("Syntax:\n\t %s -p PORT_NUMBER [-d, -debug]\n", argv[0]);
						return -1;
					}

					return port;
				}

				break;

			default:
				printf("Syntax:\n\t %s -p PORT_NUMBER [-d, -debug]\n", argv[0]);
				return -1;

				break;
		}
	}
	/* Il chiamante è il client */
	else
	{
		switch(argc)
		{
			/* Help */
			case 2:
				if(!strcmp(tolowerString(argv[1]), "-h") || !strcmp(tolowerString(argv[1]), "-help") || !strcmp(tolowerString(argv[1]), "--h") || !strcmp(tolowerString(argv[1]), "--help"))
					printf("Syntax:\n\t %s -a IP_ADDRESS -p PORT_NUMBER [-d, -debug]\n", argv[0]);
				else
					printf("For more information run \033[2;3m%s -h\033[0m\n", argv[0]);
				return -1;

				break;

			/* Caso 'corretto' */
			case 5:
				if(strcmp(tolowerString(argv[1]), "-a") || strcmp(tolowerString(argv[3]), "-p"))
				{
					printf("Syntax:\n\t %s -a IP_ADDRESS -p PORT_NUMBER [-d, -debug]\n", argv[0]);
					return -1;
				}
				else
				{
					port = atoi(argv[4]); 
					if((port < 49152 || port > 65535) && port != 47435)
					{
						printf("PORT_NUMBER must be a number between 1024 and 65535!\n");
						return -1;
					}

					if(!strcmp(tolowerString(argv[2]), "local") || !strcmp(tolowerString(argv[2]), "localhost") || !strcmp(tolowerString(argv[2]), "-l"))
						strcpy(*ip, "127.0.0.1");

					/* Controllo sintassi IP (deve contenere esattamente 3 punti e deve essere un IP valido) */
					else
					{
						int i;
						char *dotsIp = strstr(argv[2], ".");
						if(dotsIp != NULL)
							dotsIp++;
						else
						{
							printf("%s is not a valid IP address!\n", argv[2]);
							return -1;
						}

						for(i = 0; i < 2; i++)
						{
							if((dotsIp = strstr(dotsIp, ".")) == NULL)
								break;
							
							dotsIp++;
						}
						
						if(i != 2 || (inet_addr(argv[2]) == INADDR_NONE))
						{
							printf("%s is not a valid IP address!\n", argv[2]);
							return -1;
						}
						else
							strcpy(*ip, argv[2]);
					}
				}
				return port;

				break;

			case 6:
				if(strcmp(tolowerString(argv[1]), "-a") || strcmp(tolowerString(argv[3]), "-p"))
				{
					printf("Syntax:\n\t %s -a IP_ADDRESS -p PORT_NUMBER [-d, -debug]\n", argv[0]);
					return -1;
				}
				else
				{
					port = atoi(argv[4]); 
					if((port < 49152 || port > 65535) && port != 47435)
					{
						printf("PORT_NUMBER must be a number between 1024 and 65535!\n");
						return -1;
					}

					if(!strcmp(tolowerString(argv[2]), "local") || !strcmp(tolowerString(argv[2]), "localhost") || !strcmp(tolowerString(argv[2]), "-l"))
						strcpy(*ip, "127.0.0.1");

					/* Controllo sintassi IP (deve contenere esattamente 3 punti e deve essere un IP valido) */
					else
					{
						int i;
						char *dotsIp = strstr(argv[2], ".");
						if(dotsIp != NULL)
							dotsIp++;
						else
						{
							printf("%s is not a valid IP address!\n", argv[2]);
							return -1;
						}

						for(i = 0; i < 2; i++)
						{
							if((dotsIp = strstr(dotsIp, ".")) == NULL)
								break;
							
							dotsIp++;
						}
						
						if(i != 2 || (inet_addr(argv[2]) == INADDR_NONE))
						{
							printf("%s is not a valid IP address!\n", argv[2]);
							return -1;
						}
						else
							strcpy(*ip, argv[2]);
					}
				}

				if(!strcmp(tolowerString(argv[5]), "-d") || !strcmp(tolowerString(argv[5]), "-debug"))
					*debug = 1;
				else
				{
					printf("Syntax:\n\t %s -a IP_ADDRESS -p PORT_NUMBER [-d, -debug]\n", argv[0]);

					return -1;
				}

				return port;

				break;
			
			default:
				printf("Syntax:\n\t %s -a IP_ADDRESS -p PORT_NUMBER [-d, -debug]\n", argv[0]);
				return -1;
				
				break;
		}
	}
}

/* Converte un insieme di caratteri in formato network */
int* strToInt(char* inStr) {
	int len = strlen(inStr)*sizeof(int);
	int *outInt = malloc(len);
	if(outInt == NULL) {
		printf("Error while trying to \"malloc\" a new outInt!\nClosing...\n");
		exit(-1);
	} 
	bzero(outInt, len);

	for(int i=0; i<(len/sizeof(int)); i++) {
		outInt[i] = htonl((int)inStr[i]);
	}

	return outInt;
}

/* Converto un insieme di caratteri in formato host */
char* intToStr(int* inInt, int len) {
	char *outStr = malloc(len+1);
	if(outStr == NULL) {
		printf("Error while trying to \"malloc\" a new outStr!\nClosing...\n");
		exit(-1);
	} 
	bzero(outStr, len+1);

	for(int i=0; i<len; i++) {
		outStr[i] = (char)(ntohl(inInt[i]));
	}

	return outStr;
}

/* Funzione per la ricezione controllata di segmenti */
int recvSegment(int sockFd, Segment *segment, Sockaddr_in *socket, int *socketLen) {

	bzero(segment, sizeof(Segment));
	int ret;
	while(1){
            if((ret = recvfrom(sockFd, segment, sizeof(Segment), 0, (struct sockaddr*)socket, (socklen_t*)socketLen)) < 0) {
                return ret;
            }
            if((strlen(segment -> eotBit) == 0) || (strlen(segment -> seqNum) == 0) || 
               (strlen(segment -> ackNum) == 0) || (strlen(segment -> synBit) == 0) || 
               (strlen(segment -> ackBit) == 0) || (strlen(segment -> finBit) == 0) || 
               (strlen(segment -> winSize) == 0) || (strlen(segment -> cmdType) == 0) ||
               (segment -> msg == 0))
            {
                printf("\n[Error]: Empty segment received from client %s:%d\n", inet_ntoa(socket -> sin_addr), ntohs(socket -> sin_port));
            }
            else {
                break;  
            }
    }

    return ret;
}

/* Calcola il tempo trascorso da prevTime */
double elapsedTime(struct timeval prevTime) {
	struct timeval currentTime;
    double elapsedTime;

    gettimeofday(&currentTime, NULL);

    elapsedTime = (currentTime.tv_sec - prevTime.tv_sec) * 1000;      // da secondi a ms
    elapsedTime += (currentTime.tv_usec - prevTime.tv_usec) / 1000.0;   // da microsecondi a ms

    return elapsedTime;
}

/* Funzione per il calcolo dell'RTO */
double calculateRTO(struct timeval sendTime, RTT_Data *rttData) {

	double RTT = 0;
	double RTO;

	if(rttData -> sRTT == -1) {
		rttData -> sRTT = elapsedTime(sendTime);
	}
	else {
		RTT = elapsedTime(sendTime);
		// sRTT = (1-alpha)*sRTT + aplha*RTT, alpha = 0.125 = 1/8
		rttData -> sRTT = (0.875*rttData -> sRTT) + (RTT*0.125); 
		// varRTT = (1-beta)*varRTT + beta*|RTT - sRTT|, beta = 0.25 = 1/4
		rttData -> varRTT = (0.75*rttData -> varRTT) + ((RTT-rttData -> sRTT) < 0 ? (rttData->sRTT - RTT):(RTT-rttData->sRTT ))*0.25; 
	}

	// RTO = sRTT + 4*varRTT
	RTO = rttData -> sRTT + (rttData -> varRTT*4);	

	// RTO massimo 60 secondi e minimo 1 secondo
	return RTO > 60000 ? 60000 : (RTO < 1000 ? 1000 : RTO);
}

/* Ritorna la distanza tra l'ack ricevuto e il primo pacchetto presente nella finestra di invio  */
int calcSlideSize(int num1, int num2) {
	if((num1 - num2) <= 0)
		return num1-num2+(WIN_SIZE*2);
	else
		return num1-num2;
}

/* Ritorna la distanza tra due pacchetti, 0 se hanno lo stesso seqNum */
int normalizeDistance(int num1, int num2) {
	if((num1 - num2) < 0)
		return num1-num2+(WIN_SIZE*2);
	else
		return num1-num2;
}
			
/* Ritorna 1 se rcvAck è sequenzialmente inferiore di seqNum, 0 altrimenti */		
/* WIN_SIZE = 3, MAX_SEQ_NUM = 6, 1 2 3 4 5 "6" 1 2 3 ..., il numero 6 è più piccolo di 1 */
int isSeqMinor(int rcvAck, int seqNum, int distance) {
	int check;
	for(int i = -1; i < distance; i++) {
		check = (rcvAck+i)%(WIN_SIZE*2) + 1;

		if(seqNum == check)
			return 0;
    }
    return 1;
}
			
/* Inserimento ordinato di un SegQueue nella coda di ritrasmissione */						
void orderedInsertSegToQueue(SegQueue **queueHead, Segment segment, int winPos, int maxSeqNumSendable) {
	if(*queueHead == NULL) {
		*queueHead = newSegQueue(segment, winPos);
	} else {
		int distance;
		SegQueue *current = *queueHead;
		SegQueue *prev = current;

		while(current != NULL) {

			distance = normalizeDistance(maxSeqNumSendable, atoi((current -> segment).seqNum));
			if(isSeqMinor(atoi((current -> segment).seqNum), atoi(segment.seqNum), distance)) {
				
				if(current == *queueHead) {
					*queueHead = newSegQueue(segment, winPos);
					(*queueHead) -> next = current;

					return;
				}
				break;
	        }
	        prev = current;
			current = current -> next; 
		}

		prev -> next = newSegQueue(segment, winPos);
		prev -> next -> next = current;
	}
}

/* Realizza l'invio con perdita */
int randomSendTo(int sockfd, Segment *segment, struct sockaddr *socketInfo, int addrlenSocketInfo, float loss_prob) {
	int sendProb = 1 + (rand()/(float)(RAND_MAX)) * 99; // 99 = 100 - 1
    if(sendProb > loss_prob) {
        sendto(sockfd, segment, sizeof(*segment), 0, socketInfo, addrlenSocketInfo);
        return 1;
    }
    return 0;
}

/* Ritorna il codice SHA256 del file originalFileName */
char* getFileSHA256(char *originalFileName) {
    char getFileSHA256Cmd[10 + 256];
    FILE *fp;
    char *fileSHA256 = malloc(65);
    if (fileSHA256 == NULL) {
    	printf("Error while trying to \"malloc\" the fileSHA256 string!\nClosing...\n");
		exit(-1);
    }

    sprintf(getFileSHA256Cmd, "sha256sum -z \"%s\" | cut -d \" \" -f 1", originalFileName);

    fp = popen(getFileSHA256Cmd, "r");
    if (fp == NULL){
        printf("Error while trying to run the 'Get File SHA256' command!\nClosing...\n");
		exit(-1);
    }

    if(fgets(fileSHA256, 65, fp) == NULL) {
    	printf("Error while trying to run the 'Get File SHA256' command!\nClosing...\n");
		exit(-1);
    }

    if (pclose(fp) == -1) {
        printf("Error while trying to close the process for 'List file' command!\nClosing...\n");
		exit(-1);
    }

    return fileSHA256;
}

/* Copia una stringa carattere per carattere su un file */
void cpOnFile(FILE *wrFile, char *content, int len) {       
    for(int i = 0; i < len; i++) {
        fputc(content[i], wrFile);
    }
}

#endif