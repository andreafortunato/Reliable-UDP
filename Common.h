#ifndef COMMON_H
#define COMMON_H


// Dimensione massima finestra: 40 --> Sequence Number: 0->39 (modulo 40)
#define maxSeqNum 3			/* Il massimo Sequence Number è 39, quindi 2 caratteri */
#define maxAckNum 3			/* Il massimo Ack Number è 39, quindi 2 caratteri */

/**/
#define TRUE "1"
#define FALSE "0"

typedef struct _Segment
{
	char seqNum[maxSeqNum];  /* Numero di sequenza del client/server */
	char ackNum[maxAckNum];  /* Ack del segmento ricevuto */
	
	char synBit[2];          
	char ackBit[2];
	char finBit[2];

	char winSize[3];

	char msg[4096];
} Segment;

/* Inizializzazione di un nuovo client */
Segment* newSegment(char *seqNum, char *ackNum, char *synBit, char *ackBit, char *finBit, char *msg) {
	Segment *segment = (Segment*) malloc(sizeof(Segment));
	if(segment != NULL)
	{
		
	} else {
		printf("Error while trying to \"malloc\" a new Segment!\nClosing...\n");
		exit(-1);
	}

	return segment;
}


#endif