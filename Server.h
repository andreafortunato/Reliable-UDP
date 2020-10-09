#ifndef SERVER_H
#define SERVER_H

#include "common.h"

/* Struttura associata al singolo client */
typedef struct _ClientNode 
{
	int sockfd;					/* Descrittore del socket */
	char ip[16];				/* IP Address */
	int port;					/* Porta */
	struct _ClientNode *next;	/* Puntatore a ClientNode successivo */
	struct _ClientNode *prev;   /* Puntatore a ClientNode precedente */
} ClientNode;

/* Inizializzazione di un nuovo client */
ClientNode* newNode(int sockfd, char *ip, int port)
{
	ClientNode *node = (ClientNode *) malloc(sizeof(ClientNode));
	if(node != NULL)
	{
		node -> sockfd = sockfd;
		strcpy(node -> ip, ip);
		node -> port = port;
		node -> next = NULL;
		node -> prev = NULL;
	}

	return node;
}

/* Informazioni sull'orario */
char *getTime()
{
	time_t dateTime;
	struct tm* time_info;
	char *time_buff = malloc(32);


	time(&dateTime);
	time_info = localtime(&dateTime);
	strftime(time_buff, 32, "%H:%M:%S", time_info);

	return time_buff;
}

/* Chiusura socket, flag 'free' pagina logica (memoria riutilizzabile) ed azzeramento del suo contenuto */
void empty(ClientNode *clientToBeDeleted)
{
	close(clientToBeDeleted -> sockfd);
	free(clientToBeDeleted);
	bzero(clientToBeDeleted, sizeof(ClientNode));
}

/* Aggiunge un nuovo client alla lista clientList */
int addClientNode(ClientNode **clientList, ClientNode *newClient, int *clientListSize, int *maxSockFd)
{
	ClientNode *tmp1, *tmp2;

	(*clientListSize)++;

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

				return 0;
			}
			
			tmp1 = tmp1 -> next;
		}

		// Se sono arrivato alla fine della lista
		tmp2 -> next = newClient;
		newClient -> prev = tmp2;
		*maxSockFd = newClient -> sockfd;
	}

	return 0;
}

/* Elimina un client dalla lista clientList */
void deleteClientNode(ClientNode **clientList, ClientNode *client, int *clientListSize, int *maxSockFd)
{
	ClientNode *current;

	prev = *clientList;
	current = *clientList;

	(*groupListSize)--;

	// Se client è l'unico elemento presente o l'ultimo
	if(client -> next == NULL) {

		// Se è l'unico
		if(client -> prev == NULL) {
			*maxSockFd = 0;
		}
		// Se è l'ultimo
		else {
			client -> prev -> next = NULL;
			*maxSockFd = client -> prev -> sockfd;
		}

		empty(client);
	}

	/* Trovo la posizione di client, salvandola in 'current' */
	while(current != NULL)
	{
		if((current -> sockfd) == (client -> sockfd))
			break;

		current = current -> next;
	}
	/* Se l'elemento è in testa alla lista */
	if(current == *clientList)
	{
		*clientList = current -> next;
		current -> next -> prev = NULL;
	}
	/* Se l'elemento non è in testa alla lista */
	else
	{
		current -> prev -> next = current -> next;
		current -> next -> prev = current -> prev;
	}
	
	empty(client);
}

#endif