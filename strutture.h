/*
 * chatterbox Progetto del corso di LSO 2017 
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 * File strutture.h
 * Author: Fabio Catinella 517665
 *  Si dichiara che il contenuto di questo file è in ogni sua parte opera
 *  originale dell'autore
 * 
 */


#include <config.h>
#include <message.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>



//Lista di puntatori alla tabella hash
typedef struct LinkEl{
    struct HashElem* memberLink; //puntatore
    char* memberName; //nome dell'utente puntato
    struct LinkEl* next; //puntatore al successivo utente da puntare
}LinkEl;


//Lista di messaggi ricevuti
typedef struct messRec{
    message_t* received; //messaggio ricevuto
    int fdsock; //fd del mittente del messaggio
    struct messRec* next; //prossimo elemento
    int num; //numero messaggi ricevuti
}messRec;

//Elemento della tabella hash
typedef struct HashElem{
    char* nickname; //nome utente
    messRec* ricevuti; //history dell'utente
    int channel; // fd su cui è connesso
    int isOn; // indica lo stato di connessione
    struct HashElem* next; //puntatore al prossimo elemento della lista di trabocco
    int toR; // messaggi ricevuti
    struct stringList* isIn; //lista dei gruppi a cui appartiene l'utente
    //GRUPPI
    char* admin; // admin del gruppo
    int isGroup; // l'elemento è un gruppo
    struct LinkEl* GroupList; //lista di puntatori agli utenti del gruppo
} HashElem;

//coda di file descriptor
typedef struct FileDescriptorQueue{
    int* Coda; // array di int (fd)
    int first; //posizione da cui prendere l'elemento
    int last; // posizione in cui inserire l'elemento
    int dimension; // dimensione coda
} FileDescriptorQueue;

//Funzioni della coda

/* Crea una coda per i file descriptor
@arg dim     dimensione della coda
@arg Queue     puntatore alla coda

@return la coda aggiornata
*/
FileDescriptorQueue* CreateQueue(int dim,FileDescriptorQueue* Queue); //crea la coda 

/* Inserisce un file descriptor nella coda
@arg fd     file descriptor da aggiungere
@arg Queue     puntatore alla coda

@return -1 se è piena >0 se aggiunto correttamente
*/
int PushQueue(int fd, FileDescriptorQueue* Queue); //inserisce nella coda


/* Estrae un file descriptor nella coda
@arg Queue     puntatore alla coda

@return -1 se è vuota >0 (il fd) se estratto correttamente
*/
int PullQueue(FileDescriptorQueue* Queue); // estrae dalla coda

//funzioni Tabella hash

/* Funzione Hash
@arg nome    nome(chiave) della funzione hash

@return l'indice della tabella hash 
*/
int hashing(char* nome); 


/* Aggiunge un utente alla tabella hash
@arg nome     nome utente da aggiungere
@arg tabellaHash     tabella hash
@arg sock       file descriptor su cui è connesso 

@return -1 se è fallisce, 0 se aggiunto correttamente
*/
int HashAddUser(char* nome,HashElem** tabellaHash,int sock);


//Funzioni per la connessione

/* Cerca un utente nella tabella hash
@arg nick     nome utente da cercare
@arg tabellaHash        tabella hash

@return  il puntatore all'elemento della tabella hash, NULL se non esiste
*/
HashElem* HashFind(char* nick, HashElem** tabellaHash);

/* Aggiunge un utente nella tabella hash
@arg nome    nome utente da aggiungere
@arg sock       fd su cui è connesso
@arg elementoHash        lista di trabocco

@return  il puntatore all'elemento della tabella hash
*/
HashElem* aggiuntaElem(char* nome,int sock,HashElem* elementoHash);

/* Rimuove un utente nella tabella hash
@arg nome    nome utente da rimuovere
@arg tabellaHash        tabella hash

@return  il puntatore all'elemento della tabella hash aggiornata
*/
int HashDel(char* nome,HashElem** tabellaHash); 

//lista utenti connessi + funzioni

//lista stringhe
typedef struct stringList{
    char* elem; //stringa
    struct stringList* next; //puntatore elemento successivo
} stringList;


/* Aggiunge una stringa alla lista delle stringhe
@arg nome   stringa da aggiungere
@arg lista      lista delle strighe

@return  la stringa aggiornata
*/
stringList* addUserOn (char* nome,stringList* lista);

/* Rimuove una stringa alla lista delle stringhe
@arg nome   stringa da rimuovere
@arg lista      lista delle strighe

@return  la stringa aggiornata
*/
stringList* removeString(char* to_rmv, stringList* lista); 

/* Crea una stringa a partire dalla lista degli utenti online
@arg lista_connessi    lista utenti conessi

@return  una stringa contenente gli utenti online
*/
char* crea_lista_utenti(LinkEl* lista_connessi); //

//Coda messaggi ricevuti

/* Aggiunge un messaggio alla history di un utente
@arg cronologia   primo elemento della history
@arg Sock      file descriptor da associare al messaggio
@arg Mess       messaggio da aggiungere

@return  history aggiornata
*/
messRec* addMess(messRec* cronologia,int Sock,message_t* Mess); 

/* Rimuove il primo messaggio alla history di un utente
@arg cronologia   history non aggiornata

@return  history aggiornata
*/
messRec* removeMess(messRec* cronologia); 

/* Aggiunge un elemento alla lista dei puntatori
@arg source   lista puntatori non aggiornata
@arg toAdd     il puntatora da aggiungere
@return  lista aggiornata
*/
LinkEl* addLinkMem(LinkEl* source,HashElem* toAdd); 

/* Rimuove un elemento alla lista dei puntatori
@arg source   lista puntatori non aggiornata
@arg toAdd     il puntatora da rimuovere
@return  lista aggiornata
*/
LinkEl* removeGroupMem(LinkEl* source, char* toRem);

/* Controlla se un utente è nel gruppo
@arg groupname   gruppo
@arg elemento     il puntatora da aggiunger
@return  -1 se non c'è, >=0 altrimenti
*/
int isInGroup(char* groupname, stringList* elemento);

/* Cerca un elemento a partire dalla lista degli utenti online
@arg fd   file descriptor da cui risalire all'utente
@arg lista     lista utenti (puntatori ad utenti nella tabella hash)
@return  l'elemento della tabella hash
*/
HashElem* listaONFind(int fd,LinkEl* lista);



