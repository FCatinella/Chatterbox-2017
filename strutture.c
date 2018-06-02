/*
 * chatterbox Progetto del corso di LSO 2017 
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 * File strutture.c
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
#include <string.h>
#include <errno.h>
#include <strutture.h>






//elenco
HashElem* HashFind(char* nick, HashElem** tabellaHash);
HashElem* aggiuntaElem(char* nome,int sock,HashElem* elementoHash);
int HashDel(char* nome,HashElem** tabellaHash);
messRec* addMess(messRec* cronologia,int Sock,message_t* Mess);
messRec* removeMess(messRec* cronologia);
LinkEl* removeGroupMem(LinkEl* source, char* toRem);



/* Funzione Hash
@arg nome    nome(chiave) della funzione hash

@return l'indice della tabella hash 
*/
int hashing(char* nick){
    int index=0; //indice da restituire
    int i;
    for(i=0;i<strlen(nick);i++){ //per ogni carattere della stringa
        index=index+(int)nick[i]; //casto ad intero e sommo
    }
   // restituisco il valore modulo 1024 (dimensione hash table)
    return index%1024;
}


/* Aggiunge un utente alla tabella hash
@arg nome     nome utente da aggiungere
@arg tabellaHash     tabella hash
@arg sock       file descriptor su cui è connesso 

@return -1 se è fallisce, 0 se aggiunto correttamente
*/
int HashAddUser(char* nome,HashElem** tabellaHash,int sock){
    HashElem* aux=HashFind(nome,tabellaHash); //cerco l'utente nella tabella
    if(aux!=NULL){return -1;} // se è già presente, fallisco
    // altrimenti lo aggiungo
    int indice=hashing(nome);
    tabellaHash[indice]=aggiuntaElem(nome,sock,tabellaHash[indice]);
    return 0;
}


/* Aggiunge un utente nella tabella hash
@arg nome    nome utente da aggiungere
@arg sock       fd su cui è connesso
@arg elementoHash        lista di trabocco

@return  il puntatore all'elemento della tabella hash
*/
HashElem* aggiuntaElem(char* nome,int sock,HashElem* elementoHash){
    if(elementoHash==NULL){ // allocazione utente 
        elementoHash=malloc(sizeof(HashElem));
        elementoHash->nickname=malloc((MAX_NAME_LENGTH+1)*sizeof(char));
        strcpy(elementoHash->nickname,nome);
        elementoHash->ricevuti=NULL;
        elementoHash->channel=sock;
        elementoHash->isOn=0;
        elementoHash->toR=0;
        elementoHash->isIn=NULL;
        //GRUPPI
        elementoHash->admin=NULL;
        elementoHash->isGroup=0;
        elementoHash->GroupList=NULL;
        elementoHash->next=NULL;

        return elementoHash;
    }
    //aggiungo ricorsivamente
    //provo ad aggiungere in elementoHash->next
    elementoHash->next=aggiuntaElem(nome,sock,elementoHash->next);
    return elementoHash;
}


/* Rimuove un utente nella tabella hash
@arg nome    nome utente da rimuovere
@arg tabellaHash        tabella hash

@return  il puntatore all'elemento della tabella hash aggiornata
*/
int HashDel(char* nome,HashElem** tabellaHash){
    printf("- Controllo se %s è registrato\n",nome);
    int indice=hashing(nome);
    if(tabellaHash[indice]==NULL){
        return -1;
    }
    //caso in cui sia il primo elemento della lista di trabocco
    if(strcmp(tabellaHash[indice]->nickname,nome)==0){
        printf("- E' il primo elemento della lista all'indice %d\n",indice);
        HashElem* aux=tabellaHash[indice];
        if(aux->toR>0){
            printf("- Ci sono %d messaggi ricevuti e non consumati\n",aux->toR);
            while(aux->ricevuti!=NULL){ // pulizia messaggi ricevuti
                free(aux->ricevuti->received->data.buf);
                free(aux->ricevuti->received);
                aux->ricevuti=removeMess(aux->ricevuti);
            }
        }
        //rimozione dai gruppi
        while(aux->isIn!=NULL){
            printf("- Devo eliminarlo dai gruppi in cui è\n");
            stringList* aux2=aux->isIn; //lista dei gruppi in cui è registrato
            HashElem* found=HashFind(aux->isIn->elem,tabellaHash); //cerco il gruppo
            printf("- Lo elimino da %s\n",aux->isIn->elem); //tolgo il collegamento dal gruppo
            found->GroupList=removeGroupMem(found->GroupList,nome);
            free(aux->isIn->elem); //pulizia elemento del gruppo
            aux->isIn=aux->isIn->next; //continuo
            free(aux2);
        }
        free(aux->nickname);
        tabellaHash[indice]=tabellaHash[indice]->next; //l'elemento successivo al primo diventa il primo
        free(aux);
        return 0;
    }
    //non è il primo elemento della lista di trabocco
    HashElem* elem=tabellaHash[indice];
    while(elem->next!=NULL){ //scorro fino in fondo se necessario
        if(strcmp(nome,elem->next->nickname)==0){ 
            HashElem* aux=elem->next;
            if(aux->toR>0){
                while(aux->ricevuti!=NULL){ // pulizia messaggi ricevuti
                    free(aux->ricevuti->received->data.buf);
                    free(aux->ricevuti->received);
                    aux->ricevuti=removeMess(aux->ricevuti);
                }
            }
            while(aux->isIn!=NULL){ //pulizia dai gruppi
                stringList* aux2=aux->isIn;
                HashElem* found=HashFind(aux->isIn->elem,tabellaHash);
                found->GroupList=removeGroupMem(found->GroupList,nome);
                free(aux->isIn->elem);
                aux->isIn=aux->isIn->next;
                free(aux2);
            }
            free(aux->nickname);
            elem->next=elem->next->next;
            free(aux);
            return 0;
        }
        elem=elem->next; //scorro la lista di trabocco
    }
    return -1;
}


//Funzioni per la connessione

/* Cerca un utente nella tabella hash
@arg nick     nome utente da cercare
@arg tabellaHash        tabella hash

@return  il puntatore all'elemento della tabella hash, NULL se non esiste
*/
HashElem* HashFind(char* nick, HashElem** tabellaHash){
    int indice=hashing(nick); //creo l'indice
    if(tabellaHash[indice]==NULL) return tabellaHash[indice]; //se è NULL esco
    if(strcmp(nick,tabellaHash[indice]->nickname)!=0){
        HashElem* aux=tabellaHash[indice]->next;
        while(aux!=NULL){
            if(strcmp(aux->nickname,nick)==0){ //quando lo trovo restituisco il puntatore
                return aux;
            }
        aux=aux->next;
        }
        return aux;
    }
    return tabellaHash[indice];
}


//lista utenti connessi + funzioni

/* Aggiunge una stringa alla lista delle stringhe
@arg nome   stringa da aggiungere
@arg lista      lista delle strighe

@return  la stringa aggiornata
*/
stringList* addUserOn (char* nome,stringList* lista){
    if(lista==NULL){
        lista=malloc(sizeof(stringList));
        lista->elem=(char*)malloc((MAX_NAME_LENGTH+1)*sizeof(char));
        strcpy(lista->elem,nome);
        lista->next=NULL;
        return lista;
    }
    else{
        lista->next=addUserOn(nome,lista->next);
        return lista;
    }
}

/* Rimuove una stringa alla lista delle stringhe
@arg nome   stringa da rimuovere
@arg lista      lista delle strighe

@return  la stringa aggiornata
*/
stringList* removeString(char* to_rmv, stringList* lista){
    if(lista==NULL){return lista;}
    stringList* aux=lista;
    if(strcmp(lista->elem,to_rmv)==0){   
        lista=lista->next;
        free(aux->elem);
        free(aux);
        return lista;
    }
    stringList* nxt=lista->next;
    while(nxt!=NULL){
        if(strcmp(nxt->elem,to_rmv)==0){
            aux->next=nxt->next;
            free(nxt->elem);
            free(nxt);
            return lista;
        }
        aux=aux->next;
        nxt=nxt->next;
    }
    return lista;
}

/* Crea una stringa a partire dalla lista degli utenti online
@arg lista_connessi    lista utenti conessi

@return  una stringa contenente gli utenti online
*/
char* crea_lista_utenti(LinkEl* lista_connessi){
    //alloco la stringa per contenere solo un nome
    char* stringa=malloc((MAX_NAME_LENGTH+1)*sizeof(char)); 
    memset(stringa,0,MAX_NAME_LENGTH+1);
    int utenti=0;
    while(lista_connessi!=NULL){
        //rialloco la stringa per farla sempre più lunga in base a quanti utenti sono connessi
        stringa = realloc(stringa, ((utenti+1)*(MAX_NAME_LENGTH+1)*sizeof(char)));
        memset((stringa+((utenti)*(MAX_NAME_LENGTH+1))), 0, (MAX_NAME_LENGTH+1));
        //scrivo a partire dalla fine della parte scritta dall'utente precedente
        strncat((stringa+((utenti)*(MAX_NAME_LENGTH+1))), lista_connessi->memberName, (strlen(lista_connessi->memberName)));
        utenti++;
        lista_connessi=lista_connessi->next;
    }
    stringa[strlen(stringa)]='\0'; //aggiungo \0
    return stringa;
}


//Coda messaggi ricevuti

/* Aggiunge un messaggio alla history di un utente
@arg cronologia   primo elemento della history
@arg Sock      file descriptor da associare al messaggio
@arg Mess       messaggio da aggiungere

@return  history aggiornata
*/

messRec* addMess(messRec* cronologia,int Sock,message_t* Mess){
    if(cronologia==NULL){
        message_t *copia = (message_t *)malloc(sizeof(message_t));
        if(errno!=0){
            perror("ADDMESS: malloc");
            errno=0;
        }
        //copio op
        copia->hdr.op = Mess->hdr.op;
        //copio il sender
        strncpy(copia->hdr.sender, Mess->hdr.sender, MAX_NAME_LENGTH + 1);
         if(errno!=0){
            perror("ADDMESS: strncpy 1");
            errno=0;
        }
        //copio la lunghezza dei dati
        copia->data.hdr.len = Mess->data.hdr.len;
        //copio il ricevente
        strncpy(copia->data.hdr.receiver, Mess->data.hdr.receiver, MAX_NAME_LENGTH + 1);
        if(errno!=0){
            perror("ADDMESS: strncpy 2");
            errno=0;
        }
        //alloco il buffer dei dati
        copia->data.buf = (char *)malloc(copia->data.hdr.len * sizeof(char));
        //copio carattere per carattere il buffer
        for (int i = 0; i < copia->data.hdr.len; i++){
            copia->data.buf[i] = Mess->data.buf[i];
        }
        //alloco l'elemento
        cronologia=calloc(1,sizeof(messRec));
        cronologia->fdsock=Sock;
        cronologia->received=copia;
        cronologia->next=NULL;
        cronologia->num=1;
        return cronologia;
    }
    cronologia->next=addMess(cronologia->next,Sock,Mess); //aggiungo ricorsivamente
    cronologia->num++; // aggiorno il numero degli elementi successivi
    return cronologia; // restituisco la history

}


/* Rimuove il primo messaggio alla history di un utente
@arg cronologia   history non aggiornata

@return  history aggiornata
*/
messRec* removeMess(messRec* cronologia){
    if(cronologia==NULL){return cronologia;}
    messRec* aux=cronologia;
    cronologia=aux->next;
    free(aux);
    return cronologia;
}


//GRUPPI

/* Aggiunge un elemento alla lista dei puntatori
@arg source   lista puntatori non aggiornata
@arg toAdd     il puntatora da aggiungere
@return  lista aggiornata
*/
LinkEl* addLinkMem(LinkEl* source,HashElem* toAdd){
    if(source==NULL){
        source=calloc(1,sizeof(LinkEl));
        source->memberName=calloc((strlen(toAdd->nickname)+1),sizeof(char));
        strcpy(source->memberName,toAdd->nickname);
        source->next=NULL;
        source->memberLink=toAdd;
        return source;
    }
    if(strcmp(toAdd->nickname,source->memberName)!=0){
        source->next=addLinkMem(source->next,toAdd);
    }
    return source;
}

/* Rimuove un elemento alla lista dei puntatori
@arg source   lista puntatori non aggiornata
@arg toAdd     il puntatora da rimuovere
@return  lista aggiornata
*/
LinkEl* removeGroupMem(LinkEl* source, char* toRem){
    if(source==NULL){
        return source;
    }
    if(strcmp(source->memberName,toRem)==0){
        free(source->memberName);
        LinkEl*aux=source;
        source->memberLink=NULL;
        source=aux->next;
        free(aux);
        return source;
    }
    source->next=removeGroupMem(source->next,toRem);
    return source;
}

/* Cerca un elemento a partire dalla lista degli utenti online
@arg fd   file descriptor da cui risalire all'utente
@arg lista     lista utenti (puntatori ad utenti nella tabella hash)
@return  l'elemento della tabella hash
*/
HashElem* listaONFind(int fd,LinkEl* lista){
    LinkEl* aux=lista;
    while(aux!=NULL){
        if(fd==aux->memberLink->channel) return aux->memberLink;
        aux=aux->next;
    }
    return NULL;
}

/* Controlla se un utente è nel gruppo
@arg groupname   gruppo
@arg elemento     il puntatora da aggiunger
@return  -1 se non c'è, >=0 altrimenti
*/
int isInGroup(char* groupname, stringList* elemento){
    stringList* aux=elemento;
    while(aux!=NULL){
        printf("CONTROLLO %s e %s\n",groupname,aux->elem);
        if(strcmp(groupname,aux->elem)==0){
            return 0;
        }
        aux=aux->next;
    }
    return -1;

}





//CODA File Descriptor


/* Crea una coda per i file descriptor
@arg dim     dimensione della coda
@arg Queue     puntatore alla coda

@return la coda aggiornata
*/
FileDescriptorQueue* CreateQueue(int dim,FileDescriptorQueue* Queue){
    Queue=(FileDescriptorQueue*)malloc(sizeof(FileDescriptorQueue)); //alloco la coda (array circolare)
    Queue->Coda=malloc(dim*sizeof(int));
    int i =0;
    for(i=0;i<dim;i++){ //imposto tutte le celle a 0
        Queue->Coda[i]=0;
    }
    //la lista è vuota
    Queue->last=0;
    Queue->first=0;
    Queue->dimension=dim; //ha questa dimensione
    return Queue;
}


/* Inserisce un file descriptor nella coda
@arg fd     file descriptor da aggiungere
@arg Queue     puntatore alla coda

@return -1 se è piena >0 se aggiunto correttamente
*/
int PushQueue(int fd, FileDescriptorQueue* Queue){
    if(Queue->Coda[Queue->last]!=0) return -1; //se ho letto un valore !=0 allora la coda è piena
    Queue->Coda[Queue->last]=fd; //inserisco il fd
    Queue->last=((Queue->last)+1)%(Queue->dimension); //incremento l'indice di inserimento
    return fd;
}


/* Estrae un file descriptor nella coda
@arg Queue     puntatore alla coda

@return -1 se è vuota >0 (il fd) se estratto correttamente
*/
int PullQueue(FileDescriptorQueue* Queue){
    if(Queue->Coda[Queue->first]==0) return -1; //se ho letto 0 la lista è vuota
    int extracted=Queue->Coda[Queue->first]; //estraggo il fd 
    Queue->Coda[Queue->first]=0; //metto a 0 la cella che era occupata
    Queue->first=((Queue->first)+1)%(Queue->dimension); //incremento l'indice di estrazione
    return extracted;  
}
