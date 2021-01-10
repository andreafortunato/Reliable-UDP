#ifndef CLIENT_H
#define CLIENT_H

#include "Common.h"

#define flush(stdin) while(getchar() != '\n') /* Pulizia del buffer stdin */

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

#endif