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
#define EMPTY " "

typedef struct sockaddr_in Sockaddr_in;

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

/* Conversione intera stringa in minuscolo */
char *tolowerString(char *string)
{
	int i = 0;
	char *tmp = malloc(strlen(string));

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
				else if(port < 1024 || port > 65535)
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
				else if(port < 1024 || port > 65535)
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
					if(port < 1024 || port > 65535)
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
					if(port < 1024 || port > 65535)
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

void recvSegment(int sockFd, Segment *segment, Sockaddr_in *socket, int *socketLen) {

	bzero(segment, sizeof(Segment));
	while(1){
            if(recvfrom(sockFd, segment, sizeof(Segment), 0, (struct sockaddr*)socket, (socklen_t*)socketLen) < 0) {
                printf("[Error]: recvfrom failed for %s:%d\n", inet_ntoa(socket -> sin_addr), ntohs(socket -> sin_port));
                exit(-1);
            }
            if((strlen(segment -> eotBit) == 0) || (strlen(segment -> seqNum) == 0) || 
               (strlen(segment -> ackNum) == 0) || (strlen(segment -> synBit) == 0) || 
               (strlen(segment -> ackBit) == 0) || (strlen(segment -> finBit) == 0) || 
               (strlen(segment -> winSize) == 0) || (strlen(segment -> cmdType) == 0) ||
               (strlen(segment -> msg) == 0)) 
            {
                printf("\n[Error]: Empty segment received from client %s:%d\n", inet_ntoa(socket -> sin_addr), ntohs(socket -> sin_port));
            }
            else {
                break;  
            }
        }
}

#endif