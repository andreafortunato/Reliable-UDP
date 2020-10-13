#ifndef COMMON_H
#define COMMON_H


// Dimensione massima finestra: 40 --> Sequence Number: 0->39 (modulo 40)
#define maxSeqNum 3			/* Il massimo Sequence Number è 39, quindi 2 caratteri */
#define maxAckNum 3			/* Il massimo Ack Number è 39, quindi 2 caratteri */


typedef struct _Segment
{
	char seqNum[maxSeqNum];
	char ackNum[maxAckNum];
	
	char synBit[2];
	char ackBit[2];

	char msg[4096];
} Segment;

#endif