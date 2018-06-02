/*
 * chatterbox Progetto del corso di LSO 2017 
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 * File connections.c
 * Author: Fabio Catinella 517665
 *  Si dichiara che il contenuto di questo file è in ogni sua parte opera
 *  originale dell'autore
 * 
 */
#ifndef CONNECTIONS_H_
#define CONNECTIONS_H_

#define MAX_RETRIES     10
#define MAX_SLEEPING     3
#if !defined(UNIX_PATH_MAX)
#define UNIX_PATH_MAX  64
#endif

#include <message.h>
#include <ops.h>
#include <stdlib.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <connections.h>

//dichiarazioni funzioni
int readAll(long fd, void* buff, int size);
int sendRequest(long fd, message_t *msg);
int sendHeader(long fd,message_hdr_t* hdr);
int sendData(long fd, message_data_t *msg);
int writeAll(long fd,void* buff, int size);


/**
 * @file  connection.h
 * @brief Contiene le funzioni che implementano il protocollo 
 *        tra i clients ed il server
 */

/**
 * @function openConnection
 * @brief Apre una connessione AF_UNIX verso il server 
 *
 * @param path Path del socket AF_UNIX 
 * @param ntimes numero massimo di tentativi di retry
 * @param secs tempo di attesa tra due retry consecutive
 *
 * @return il descrittore associato alla connessione in caso di successo
 *         -1 in caso di errore
 */
int openConnection(char* path, unsigned int ntimes, unsigned int secs){
    int csock; 
    int volte=0; //numero di tentativi già fatti
    struct sockaddr_un cisocket;
    strncpy(cisocket.sun_path,path,UNIX_PATH_MAX);
    cisocket.sun_family=AF_UNIX;
    csock=socket(AF_UNIX,SOCK_STREAM,0);
    if(errno!=0){perror("creazione");}
    while (volte<ntimes ){
        //connessione al socket
        connect(csock,(struct sockaddr*)&cisocket,sizeof(cisocket));
        if(errno!=ENOENT){return csock;}
        printf("ASPETTO\n");
        volte++;
        sleep(secs);
    
    }
   if(volte==ntimes){return -1;}
   return csock;
}
// -------- server side ----- 
/**
 * @function readHeader
 * @brief Legge l'header del messaggio
 *
 * @param fd     descrittore della connessione
 * @param hdr    puntatore all'header del messaggio da ricevere
 *
 * @return <=0 se c'e' stato un errore 
 *         (se <0 errno deve essere settato, se == 0 connessione chiusa) 
 */
int readHeader(long connfd, message_hdr_t *hdr){
    long ris=1;
    //leggo header op
    ris=(long)(readAll(connfd,&(hdr->op),sizeof(op_t)));
    if(ris<=0) { return ris;}
    //leggo header sender
    ris=readAll(connfd,(hdr->sender),sizeof(char)*(MAX_NAME_LENGTH+1));
    if(ris<=0) { return ris;}
    return (int)ris;
}
/**
 * @function readData
 * @brief Legge il body del messaggio
 *
 * @param fd     descrittore della connessione
 * @param data   puntatore al body del messaggio
 *
 * @return <=0 se c'e' stato un errore
 *         (se <0 errno deve essere settato, se == 0 connessione chiusa) 
 */
int readData(long fd, message_data_t *data){
    int ris;
    int da_ret=0;
    memset(data, 0, sizeof(message_data_t));
    //leggo il receiver
    ris=readAll(fd,&data->hdr.receiver,(MAX_NAME_LENGTH+1)*sizeof(char));
    if(ris<=0){perror("read receiver");return ris;}
    da_ret+=ris; //aggiorno la somma dei byte letti
    // leggo la lunghezza dei dati
    ris=readAll(fd,&data->hdr.len,sizeof(int));
    if(ris<=0){perror("read len");return ris;}
    da_ret+=ris;
    if(data->hdr.len>0){ // se è maggiore
        data->buf=calloc((data->hdr.len),sizeof(char));
        ris=readAll(fd,(data->buf),(data->hdr.len)*sizeof(char));
        if(ris<=0){perror("read buff");return ris;}
        da_ret+=ris;
    }
    else {
        data->buf=NULL;
    } 
    return da_ret;
} 

/**
 * @function readMsg
 * @brief Legge l'intero messaggio
 *
 * @param fd     descrittore della connessione
 * @param data   puntatore al messaggio
 *
 * @return <=0 se c'e' stato un errore
 *         (se <0 errno deve essere settato, se == 0 connessione chiusa) 
 */
int readMsg(long fd, message_t *msg){
    int ris;
    memset(&(msg->hdr),0,sizeof(message_hdr_t));
    //leggo header
    ris=readHeader(fd,&(msg->hdr));
    if(ris<=0) { return ris;}
    //leggo i data
    ris=readData(fd,&msg->data);
    if(ris<=0) { return ris;}
    return ris;
}

/* da completare da parte dello studente con altri metodi di interfaccia */


/*
* @function readAll
* @brief legge "size" byte da fd e li scrive in buff
* 
* @param fd   descrittore della connessione
* @param buff puntatore al buffer 
* @param size dimensione in byte da leggere
*
* @return numero totale di byte letti
*/


int readAll(long fd, void* buff, int size){
    int mancante=size;
    int letto=0;
    int totale=0;
    while(mancante>0){       
        letto=read(fd,buff,size);
        mancante=mancante-letto;
        totale=totale+letto;
        if(letto==0) return totale;
        if(letto<0) return -1;
    }
    return totale;
}

// ------- client side ------
/**
 * @function sendRequest
 * @brief Invia un messaggio di richiesta al server 
 *
 * @param fd     descrittore della connessione
 * @param msg    puntatore al messaggio da inviare
 *
 * @return <=0 se c'e' stato un errore
 */
int sendRequest(long fd, message_t *msg){
    long ris;
    ris=(long)(sendHeader(fd,&msg->hdr));
    if(ris<=0){perror("sendhdr request");}
    ris=(long)(sendData(fd,&msg->data));
    if(ris<=0){perror("senddata request");}
    return (int)ris;
}

/**
 * @function sendData
 * @brief Invia il body del messaggio al server
 *
 * @param fd     descrittore della connessione
 * @param msg    puntatore al messaggio da inviare
 *
 * @return <=0 se c'e' stato un errore
 */
int sendData(long fd, message_data_t *msg){
    long ris;
    ris=(long)writeAll((int)fd,msg->hdr.receiver,(MAX_NAME_LENGTH+1)*sizeof(char));
    if(ris<=0) {perror("writeReceiver"); return ris;}
    ris=writeAll((int)fd,&msg->hdr.len,sizeof(int));
    if(ris<=0) {perror("writeLength"); return ris;}
    if(msg->hdr.len>0){
        ris=writeAll((int)fd,msg->buf,(msg->hdr.len)*sizeof(char));
        if(ris<=0) {perror("writeDati"); return ris;}
    }
    return (int)ris;}


/* da completare da parte dello studente con eventuali altri metodi di interfaccia */

/*
* @function sendHeader
* @brief invia l'header di un messaggio
* 
* @param fd   descrittore della connessione
* @param hdr   header da inviare
*
* @return numero totale di byte inviati
*/

int sendHeader(long fd,message_hdr_t* hdr){
    long ris;
    int da_ret=0;
    ris=writeAll((int)fd,&(hdr->op),sizeof(op_t));
    if(ris<=0) {perror("SendHeader:writeOp"); return ris;}
    da_ret+=ris;
    ris=writeAll((int)fd,&hdr->sender,(MAX_NAME_LENGTH+1)*sizeof(char));
    if(ris<=0) {perror("SendHeader:writeSender"); return ris;}
    da_ret+=ris;
    return (int)da_ret;}


/*
* @function writeAll
* @brief scrive "size" byte in fd da buff
* 
* @param fd   descrittore della connessione
* @param buff puntatore al buffer 
* @param size dimensione in byte da scrivere
*
* @return numero totale di byte scritti
*/


int writeAll(long fd,void* buff, int size){
    int mancante=size;
    int scritto=0; 
    int totale=0;
    while(mancante>0){
        scritto=write(fd,buff,size);
        mancante=mancante-scritto;
        totale=totale+scritto;
        if(scritto==0) return totale;
        if(scritto<0) return -1;
    }
    return totale;
}
#endif /* CONNECTIONS_H_ */
