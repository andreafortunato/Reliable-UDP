#ifndef SERVER_H
#define SERVER_H

#include "Common.h"

/* Struttura associata al singolo client */
typedef struct _ClientNode {
	unsigned int sockfd;					/* Descrittore del socket */
	char ip[16];							/* IP Address */
	unsigned int clientPort;				/* Porta sorgente del Client */
 
	pthread_t clientTid;					/* Thread ID temporaneo associato al Client */
	unsigned int serverPort;				/* Porta del server riservata al Client*/

	unsigned int lastSeqClient;				/* Ultimo numero di sequenza ricevuto dal CLient */
	unsigned int lastSeqServer;				/* Ultimo numero di sequenza inviato al Client */

	struct _ClientNode *next;				/* Puntatore a ClientNode successivo */
	struct _ClientNode *prev;   			/* Puntatore a ClientNode precedente */
} ClientNode;

/* Struttura per il passaggio di dati alla creazione di un nuovo thread */
typedef struct _ThreadArgs {
	struct sockaddr_in clientSocket;
	char seqNumClient[11];
} ThreadArgs;

ThreadArgs* newThreadArgs(struct sockaddr_in clientSocket, char *seqNumClient) {
	ThreadArgs *threadArgs = (ThreadArgs *) malloc(sizeof(ThreadArgs));
	if(threadArgs != NULL)
	{
		threadArgs -> clientSocket = clientSocket;
		strcpy(threadArgs -> seqNumClient, seqNumClient);
	} else {
		printf("Error while trying to \"malloc\" a new ThreadArgs!\nClosing...\n");
		exit(-1);
	}

	return threadArgs;
}

/* Inizializzazione di un nuovo client */
ClientNode* newNode(unsigned int sockfd, char *ip, unsigned int clientPort, pthread_t tid, unsigned int serverPort, char *lastSeqClient) {
	ClientNode *node = (ClientNode *) malloc(sizeof(ClientNode));
	if(node != NULL)
	{
		node -> sockfd = sockfd;
		strcpy(node -> ip, ip);
		node -> clientPort = clientPort;

		node -> clientTid = tid;
		node -> serverPort = serverPort;

		node -> lastSeqClient = atoi(lastSeqClient);
		node -> lastSeqServer = 0;

		node -> next = NULL;
		node -> prev = NULL;
	} else {
		printf("Error while trying to \"malloc\" a new ClientNode!\nClosing...\n");
		exit(-1);
	}

	return node;
}

/* Informazioni sull'orario */
char *getTime() {
	time_t dateTime;
	struct tm* time_info;
	char *time_buff = malloc(32);


	time(&dateTime);
	time_info = localtime(&dateTime);
	strftime(time_buff, 32, "%H:%M:%S", time_info);

	return time_buff;
}

/* Chiusura socket, flag 'free' pagina logica (memoria riutilizzabile) ed azzeramento del suo contenuto */
void empty(ClientNode *clientToBeDeleted) {
	close(clientToBeDeleted -> sockfd);
	free(clientToBeDeleted);
	bzero(clientToBeDeleted, sizeof(ClientNode));
}

/* Aggiunge un nuovo client alla lista clientList */
void addClientNode(ClientNode **clientList, ClientNode *newClient, int *clientListSize, int *maxSockFd) {
	ClientNode *tmp1, *tmp2;


	if((*clientListSize) == 0)
	{
		*clientList = newClient;
		*maxSockFd = newClient -> sockfd;
	}
	else
	{
		tmp1 = *clientList;

		while(tmp1 != NULL)
		{	
			tmp2 = tmp1;

			if((newClient -> sockfd) < (tmp1 -> sockfd)) {
				
				// Se tmp1 è la testa
				if(tmp1 == *clientList) {
					*clientList = newClient;
					newClient -> next = tmp1;
					tmp1 -> prev = newClient;
				}	
				// Se tmp1 non è la testa
				else {
					newClient -> next = tmp1;
					newClient -> prev = tmp1 -> prev;
					tmp1 -> prev -> next = newClient;
					tmp1 -> prev = newClient;
				}

				(*clientListSize)++;
				return;
			}
			
			tmp1 = tmp1 -> next;
		}

		// Se sono arrivato alla fine della lista
		tmp2 -> next = newClient;
		newClient -> prev = tmp2;
		*maxSockFd = newClient -> sockfd;
	}
	(*clientListSize)++;
}

/* Elimina un client dalla lista clientList */
void deleteClientNode(ClientNode **clientList, ClientNode *client, int *clientListSize, int *maxSockFd) {
	ClientNode *current;

	current = *clientList;

	// Se client è l'unico elemento presente o l'ultimo
	if(client -> next == NULL) {

		// Se è l'unico
		if(client -> prev == NULL) {
			printf("Eliminazione in testa, unico elemento\n");
			*maxSockFd = 0;
		}
		// Se è l'ultimo
		else {
			printf("Eliminazione in coda\n");
			client -> prev -> next = NULL;
			*maxSockFd = client -> prev -> sockfd;
		}

		empty(client);
		(*clientListSize)--;
		return;
	}

	/* Trovo la posizione di client, salvandola in 'current' */
	while(current != NULL)
	{
		if((current -> sockfd) == (client -> sockfd))
			break;

		current = current -> next;
	}
	printf("Porta da eliminare: %d\n", current->clientPort);
	/* Se l'elemento è in testa alla lista */
	if(current == *clientList)
	{
		printf("Eliminazione in testa\n");
		*clientList = current -> next;
		current -> next -> prev = NULL;
	}
	/* Se l'elemento non è in testa alla lista */
	else
	{
		printf("Eliminazione al centro\n");
		current -> prev -> next = current -> next;
		current -> next -> prev = current -> prev;
	}
	
	empty(client);
	(*clientListSize)--;

}

/* TEMP - Stampa lista */
void printList(ClientNode *clientList) {
	while(clientList != NULL)
	{
		printf("\n");
		if(clientList->prev)
			printf("Prev: %d\n", clientList->prev->sockfd);
		else
			printf("Prev: NULL\n");

		printf("Sockfd: %d\nIP: %s\nPort: %d\n", clientList->sockfd, clientList->ip, clientList->clientPort);
		
		if(clientList->next)
			printf("Next: %d\n", clientList->next->sockfd);
		else
			printf("Next: NULL\n");

		clientList = clientList->next;
	}
	printf("|-----------------------------|\n");
}

#endif