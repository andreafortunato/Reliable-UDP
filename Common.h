#ifndef COMMON_H
#define COMMON_H

/* Dimensione campo Seq/Ack 11 perchè il massimo valore rappresentabile
   in TCP è (2^16)-1 = 4294967295, quindi 11 caratteri */
// Provare 2**16 pow(2,16) per elevare 2 alla 16
#define MAX_SEQ_ACK_NUM 11
#define BIT 2

/**/
#define TRUE "1"
#define FALSE "0"
#define EMPTY " \0"


typedef struct _Segment
{
	char eotBit[BIT];				/* End Of Transmission Bit: posto ad 1 se l'operazione (download/upload/list)
									   è conclusa con successo, 0 altrimenti */
	char seqNum[MAX_SEQ_ACK_NUM];	/* Numero di sequenza del client/server */
	char ackNum[MAX_SEQ_ACK_NUM];	/* Ack del segmento ricevuto */
	
	char synBit[BIT];          
	char ackBit[BIT];
	char finBit[BIT];

	char winSize[3];

	char cmdType[2];				/* Operazione richiesta */
	char msg[4081];
} Segment;


/* ***************************************************************************************** */


/* Inizializzazione di un nuovo client */
Segment* mallocSegment(char *seqNum, char *ackNum, char *synBit, char *ackBit, char *finBit, char *cmdType, char *msg) {
	Segment *segment = (Segment*) malloc(sizeof(Segment));
	if(segment != NULL)
	{
		strcpy(segment -> eotBit, "1");

		strcpy(segment -> seqNum, seqNum);
		strcpy(segment -> ackNum, ackNum);

		strcpy(segment -> synBit, synBit);
		strcpy(segment -> ackBit, ackBit);
		strcpy(segment -> finBit, finBit);
		
		strcpy(segment -> winSize, "5");
		
		strcpy(segment -> cmdType, cmdType);
		strcpy(segment -> msg, msg);
	} else {
		printf("Error while trying to \"malloc\" a new Segment!\nClosing...\n");
		exit(-1);
	}

	return segment;
}

/* Inizializzazione di un nuovo client */
void newSegment(Segment *segment, char *seqNum, char *ackNum, char *synBit, char *ackBit, char *finBit, char *cmdType, char *msg) {

	bzero(segment, sizeof(Segment));

	strcpy(segment -> eotBit, "1");

	strcpy(segment -> seqNum, seqNum);
	strcpy(segment -> ackNum, ackNum);

	strcpy(segment -> synBit, synBit);
	strcpy(segment -> ackBit, ackBit);
	strcpy(segment -> finBit, finBit);
	
	strcpy(segment -> winSize, "5");
	
	strcpy(segment -> cmdType, cmdType);
	strcpy(segment -> msg, msg);
}



#endif