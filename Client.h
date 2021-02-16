#ifndef CLIENT_H
#define CLIENT_H

#include "Common.h"

#define flush(stdin) while(getchar() != '\n') /* Pulizia del buffer stdin */

static const char downloadGradient[50][12] = {"255;0;0", "249;5;0", "244;10;0", "239;15;0", "234;20;0", "228;26;0", "223;31;0", "218;36;0", "213;41;0", "208;46;0", "202;52;0", "197;57;0", "192;62;0", "187;67;0", "182;72;0", "176;78;0", "171;83;0", "166;88;0", "161;93;0", "156;98;0", "150;104;0", "145;109;0", "140;114;0", "135;119;0", "130;124;0", "124;130;0", "119;135;0", "114;140;0", "109;145;0", "104;150;0", "98;156;0", "93;161;0", "88;166;0", "83;171;0", "78;176;0", "72;182;0", "67;187;0", "62;192;0", "57;197;0", "52;202;0", "46;208;0", "41;213;0", "36;218;0", "31;223;0", "26;228;0", "20;234;0", "15;239;0", "10;244;0", "5;249;0", "0;255;0"};

/* Struttura per il passaggio di dati alla creazione di un nuovo thread */
typedef struct _ThreadArgs {
	Sockaddr_in serverSocket;
	int sockfd;
	char isAck[BIT];

} ThreadArgs;

ThreadArgs* newThreadArgs(Sockaddr_in serverSocket, int sockfd, char *isAck) {
	ThreadArgs *threadArgs = (ThreadArgs *) malloc(sizeof(ThreadArgs));
	if(threadArgs != NULL)
	{	
		bzero(threadArgs, sizeof(ThreadArgs));
		threadArgs -> serverSocket = serverSocket;
		threadArgs -> sockfd = sockfd;
		strcpy(threadArgs -> isAck, isAck);
	} else {
		printf("Error while trying to \"malloc\" a new ThreadArgs!\nClosing...\n");
		exit(-1);
	}

	return threadArgs;
}

/* Stampa del menù */
void makeClientMenu()
{
	wchar_t upsx = 0x259b;			/* ▛ */
	wchar_t up = 0x2580;			/* ▀ */
	wchar_t updx = 0x259c;			/* ▜ */
	wchar_t sx = 0x258c;			/* ▌ */
	wchar_t separator = 0x254d;		/* ╍ */
	wchar_t dx = 0x2590;			/* ▐ */
	wchar_t downsx = 0x2599;		/* ▙ */
	wchar_t down = 0x2584;			/* ▄ */
	wchar_t downdx = 0x259f;		/* ▟ */
	wchar_t u_grave = 0x00d9;		/* Ù */
	wprintf(L"\n%lc", upsx);
	for(int i = 0; i < 47; i++)
		wprintf(L"%lc", up);
	wprintf(L"%lc\n%lc\t\t      \033[1mMEN%lc\033[0m\t\t\t%lc\n%lc", updx, sx, u_grave, dx, sx); /* 1;5m = Grassetto;Intermittenza, 0m = Reset */
	for(int i = 0; i < 47; i++)
		wprintf(L"%lc", separator);
	wprintf(L"%lc\n", dx);
	wprintf(L"%lc 1. List of available files.\t\t\t%lc\n", sx, dx);
	wprintf(L"%lc 2. Download file.\t\t\t\t%lc\n", sx, dx);
	wprintf(L"%lc 3. Upload file.\t\t\t\t%lc\n", sx, dx);
	wprintf(L"%lc 4. Quit.\t\t\t\t\t%lc\n%lc", sx, dx, downsx);
	for(int i = 0; i < 47; i++)
		wprintf(L"%lc", down);
	wprintf(L"%lc\n", downdx);
}

/*Operazione scelta dall'utente (tra le opzioni del menù)
RETURN VALUE:
		Valore inserito dall'utente */
int clientChoice()
{
	int choice;
	int ret;

	/* Stampa del menu */
	makeClientMenu();

	do {
		wprintf(L"Choose an option: ");
		ret = scanf("%d", &choice);
		flush(stdin);
		if(ret != 1)
			wprintf(L"\033[1A\033[2KInvalid option!\n"); /* 1A = Vai alla riga superiore, 2K = Cancella la riga corrente */
		else if(choice < 1 || choice > 4)
			wprintf(L"\033[1A\033[2KInvalid option (%d)!\n", choice); /* 1A = Vai alla riga superiore, 2K = Cancella la riga corrente */
	} while(choice < 1 || choice > 4);

	return choice;
}

void printDownloadStatusBar(int bytesRecv, int totalBytes, int *oldHalfPercentage) {
	wchar_t ok = 0x2588;			/* █ */
	// wchar_t no = 0x2591;			/* ░ */

	int halfPercentage = (bytesRecv*50)/totalBytes; // Parte inferiore
	
	int old = *oldHalfPercentage;

	int toWrite = halfPercentage - old;

	// FUNZIONA MA OGNI VOLTA RISCRIVE TUTTO!
	// if(toWrite > 0) {
	// 	wprintf(L"[");
	//     for (int i = 0; i < halfPercentage; i++) {
	//         wprintf(L"%lc", 0x2588);
	//     }
	//     for (int i = 0; i < 50-halfPercentage; i++) {
	//         wprintf(L"%lc", 0x2591);
	//     }
	//     wprintf(L"]");

	//     *oldHalfPercentage = halfPercentage;
	// }

	
	if(toWrite > 0) {
		wprintf(L"\r\033[%dC", old+2);

		for (int i = 0; i < toWrite; i++) {
			wprintf(L"\033[38;2;%sm%lc\033[0m", downloadGradient[old+i], ok);
		}

		*oldHalfPercentage = halfPercentage;
	}

	return;
	
	// 0	1	:	[--------------------------------------------------] : null
	// 2	3	:	[#-------------------------------------------------] : 1  - 1 avanti
	// 4	5	:	[##------------------------------------------------] : 2  - 2 avanti
	// 6	7	:	[###-----------------------------------------------] : 3  - 3 avanti
	// 8	9	:	[####----------------------------------------------] : 4  - 4 avanti
	// 10	11	:	[#####---------------------------------------------] : 5  - 5 avanti
	// 12	13	:	[######--------------------------------------------] : 6  - 6 avanti
	// 14	15	:	[#######-------------------------------------------] : 7  - 7 avanti
	// 16	17	:	[########------------------------------------------] : 8  - 8 avanti
	// 18	19	:	[#########-----------------------------------------] : 9  - 9 avanti
	// 20	21	:	[##########----------------------------------------] : 10 - 10 avanti

	// 96	97	:	[################################################--] : 48 - 48 avanti
	// 98	99	:	[#################################################-] : 49 - 49 avanti
	//   100	:	[##################################################] : 50 - 50 avanti
}

#endif