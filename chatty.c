/*
 * chatterbox Progetto del corso di LSO 2017 
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 * File chatty.c
 * Author: Fabio Catinella 517665
 *  Si dichiara che il contenuto di questo file è in ogni sua parte opera
 *  originale dell'autore
 * 
 */

/**
 * @file chatty.c
 * @brief File principale del server chatterbox
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>

/* inserire gli altri include che servono */
#include <fcntl.h>
#include <sys/types.h>
#include <ops.h>
#include <config.h>
#include <stats.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <message.h>
#include <connections.h>
#include <strutture.h>
#include <sys/select.h>

//DEFINE
#define FALLIMENTO {printf("FALLIMENTO\n");return -1;}
#define UNIX_PATH_MAX  64

/* struttura che memorizza le statistiche del server, struct statistics 
 * e' definita in stats.h.
 *
 */

struct statistics  chattyStats = { 0,0,0,0,0,0,0 };


static void usage(const char *progname) {
    fprintf(stderr, "Il server va lanciato con il seguente comando:\n");
    fprintf(stderr, "  %s -f conffile\n", progname);
}

//dati di configurazione del server
char* UnixPath=NULL;
int MaxConnections=0;
int ThreadsInPool=0;
int MaxMsgSize=0;
int MaxFileSize=0;
int MaxHistMsgs=0;
char* DirName=NULL;
char*StatFileName=NULL;

//Messaggio letto
message_t* messletto;

//bit pulizia
sig_atomic_t clean_bit;

//bit stat
sig_atomic_t stat_bit=0;

//TabellaHashCentrale
HashElem** userHashTable=NULL;

//lista utenti connessi
LinkEl* lista_ON=NULL;
int nusersON=0;

//sockwt
int sock;

//set per la SELECT
fd_set set;
fd_set rdset;
int fdnum=0;

//coda File Descriptor
FileDescriptorQueue* fd_queue;

//numeroThreadattvi
int threadActive;

//dichiarazioni funzioni
int exdatoi(char* buffer); //estrae i dati dal file di configurazione e lo casta in int
char* exdato(char* buffer); //estrae il dato dal file di configurazione


//Lock
pthread_mutex_t lock_userHashTable[32];
pthread_mutex_t lock_lista_ON=PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t lock_threadActive=PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t lock_set=PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t lock_fd_queue=PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t lock_stat=PTHREAD_MUTEX_INITIALIZER;
//cond Variable
pthread_cond_t cond_fd_queue_Push=PTHREAD_COND_INITIALIZER;
pthread_cond_t cond_fd_queue_Pull=PTHREAD_COND_INITIALIZER;


//Funzioni per il PARSER
//------------------------------
int exdatoi(char* buffer){
/*estrae i dati dal file di configurazione e lo casta in int*/

          int i=0;
          while((buffer[i]!='=') && (buffer[i]!='\n') && (buffer[i]!='\0')){i++;} //cerco l'inizio della parola 
          int s=0;
          i+=2; // mi sposto di due caratteri (nel file di configurazione i dati sono dopo un "=" e uno " ")
          errno=0;
          char* dato=(char*)malloc(100*sizeof(char));
          memset(dato,0,100*sizeof(char));
          if(errno!=0){perror("malloc");}
          while((buffer[i]!='\n') && (buffer[i]!='\0')){
                dato[s]=buffer[i];   //scrivo carattere per carattere fino alla fine 
                i++;
                s++;
          }
          int result=atoi(dato); //cast in intero
          free(dato);  
          return result;
}

//senza cast
char* exdato(char* buffer){
/*estrae i dati dal file di configurazione*/
          int i=0;
          while((buffer[i]!='=') && (buffer[i]!='\n') && (buffer[i]!='\0')){i++;} //cerco l'inizio della parola
          int s=0;
          i+=2; // mi sposto di due caratteri (nel file di configurazione i dati sono dopo un "=" e uno " ")
          errno=0;
          char* dato=(char*)malloc(100*sizeof(char));
          memset(dato,0,100*sizeof(char));
          if(errno!=0){perror("malloc");}
          while((buffer[i]!='\n') && (buffer[i]!='\0')){
                dato[s]=buffer[i];
                i++;
                s++;
          }        
          return dato;
}
//--------------------------------------------



//WORKER 
//restituisce 0 in caso di successo, -1 altrimenti
//---------------------------------------------
int worker(message_t rice, int chan){
    //dichiarazioni delle variabili che mi serviranno nel worker
    int esito;
    message_t* answer=calloc(1,sizeof(message_t)); //messaggio di risposta al client sender
    message_t* toRec=calloc(1,sizeof(message_t)); // messaggio da inviare al client receiver (se presente)
    HashElem* toAdd=NULL;
    HashElem* found=NULL;
    int hashed_lock=hashing(rice.hdr.sender)%32; //indice della lock del sender
    int index_lock_receiver=0; //indice della lock del receiver
    int index=0;
    errno=0; //setto sempre errno a 0 per non portarmi errori troppo a lungo
    char* ListaStringa =NULL; //stringa contenente la lista degli utenti online da inviare al client che la richiede
    FILE* inFile; //puntatore al file dove scrivere i file ricevuti dal un client che lo invia
    message_data_t fileRice; // file ricevuto in caso di POSTFILE_OP
  
    //In caso mi arrivi un messaggio senza sender invio una OP_FAIL
    if (rice.hdr.sender == NULL || strlen(rice.hdr.sender) == 0) {
    setHeader(&(answer->hdr), OP_FAIL, "");
    sendHeader(chan, &(answer->hdr));
    return -1;
    }

    //Switch operazioni richieste
    switch(rice.hdr.op){
        case REGISTER_OP:
            // Registrazione di un nuovo utente
            printf("------REGISTER_OP------\n");

            // Aggiungo l'utente alla tabella hash
            pthread_mutex_lock(&lock_userHashTable[hashed_lock]); //LOCK
            esito=HashAddUser(rice.hdr.sender,userHashTable,chan);
            pthread_mutex_unlock(&lock_userHashTable[hashed_lock]); //UNLOCK
           
            printf("- Aggiungo %s\n",rice.hdr.sender);
            if(esito==-1){
                // se è già presente invio una OP_NICK_ALREADY
                printf("- ERRORE:utente già presente\n");
                memset(answer,0,sizeof(message_t)); //free
                setHeader(&(answer->hdr),OP_NICK_ALREADY,rice.data.hdr.receiver);
                sendHeader(chan,&(answer->hdr));
                free(answer);
                free(toRec);

                //aggiornamento stat
                pthread_mutex_lock(&lock_stat);
                chattyStats.nerrors++;
                pthread_mutex_unlock(&lock_stat);
                //------------------------------

                return -1; 
                break;
            }

            //aggiornamento stat
            pthread_mutex_lock(&lock_stat);
            chattyStats.nusers++;
            pthread_mutex_unlock(&lock_stat);
            //------------------------------

            printf("------FINE REGISTER_OP------\n");

        case CONNECT_OP:
            //Connessione utente 
            printf("------CONNECT_OP------\n");

            printf("- prendo la lock %d\n",hashed_lock);
            pthread_mutex_lock(&lock_userHashTable[hashed_lock]); //LOCK
            //cerco l'utente da connettere nella tabella hash
            toAdd=HashFind(rice.hdr.sender,userHashTable);
            if(toAdd!=NULL){
                if(toAdd->isOn==0){ //se è offline 
                    toAdd->isOn=1;
                    toAdd->channel=chan; // mi segno il fd su cui è connesso
                    printf("- %s connesso sul filedescriptor %d\n",rice.hdr.sender,chan);
                    pthread_mutex_lock(&lock_lista_ON); 
                    lista_ON=addLinkMem(lista_ON,toAdd); //lo aggiungo alla lista degli utenti online
                    nusersON++; // ed aggiorno il numero
                    pthread_mutex_unlock(&lock_lista_ON);

                     //aggiornamento stat
                    pthread_mutex_lock(&lock_stat);
                    chattyStats.nonline++;
                    pthread_mutex_unlock(&lock_stat);
                    //------------------------------
                }
                
            }
            else {
                // utente sconosciuto
                printf("- NICK_UNKNOW\n");
                printf("- lascio la lock %d\n",hashed_lock);
                pthread_mutex_unlock(&lock_userHashTable[hashed_lock]); //UNLOCK
                memset(answer,0,sizeof(message_t)); //free
                setHeader(&(answer->hdr),OP_NICK_UNKNOWN,rice.data.hdr.receiver); //setto ed invio un messaggio di NICK_UNKNOWN
                sendHeader(chan,&(answer->hdr));
                free(answer);
                free(toRec);

                //aggiornamento stat
                pthread_mutex_lock(&lock_stat);
                chattyStats.nerrors++;
                pthread_mutex_unlock(&lock_stat);
                //------------------------------

                return -1;
                break;
            }
            printf("------FINE CONNECT_OP------\n");

        case USRLIST_OP:
            // Invio lista connessi
            printf("------USRLIST_OP------\n");

            pthread_mutex_lock(&lock_lista_ON);
            ListaStringa=crea_lista_utenti(lista_ON); //scrivo gli utenti online in una stringa
            pthread_mutex_unlock(&lock_lista_ON);

            printf("- lascio la lock %d\n",hashed_lock);
            pthread_mutex_unlock(&lock_userHashTable[hashed_lock]); //UNLOCK

            memset(answer,0,sizeof(message_t)); //free
            setHeader(&(answer->hdr),OP_OK,rice.data.hdr.receiver);
            //invio la stringa
            setData(&(answer->data),rice.data.hdr.receiver,ListaStringa,nusersON*(MAX_NAME_LENGTH+1));
            sendHeader(chan,&(answer->hdr)); 
            sendData(chan,&(answer->data));
            free(ListaStringa);
            free(answer);
            free(toRec); 
            printf("------FINE USRLIST_OP------\n");
            return 0;
            break;

        case POSTTXT_OP:
            //invio di un messaggio ad un client
            printf("------POSTTXT_OP------\n");

            printf("- Il messaggio ha la lunghezza giusta:");
            printf("- len:%d e MaxMsgFile:%d\n",rice.data.hdr.len,MaxMsgSize);
            if(rice.data.hdr.len>MaxMsgSize){
                //se il messaggio è troppo lungo secondo i parametri di configurazione 
                //allora invio un messaggio di errore
                memset(answer,0,sizeof(message_t)); //free
                setHeader(&(answer->hdr),OP_MSG_TOOLONG,rice.data.hdr.receiver);
                sendHeader(chan,&(answer->hdr));
                free(answer);
                free(toRec);

                //aggiornamento stat
                pthread_mutex_lock(&lock_stat);
                chattyStats.nerrors++;
                pthread_mutex_unlock(&lock_stat);
                //------------------------------
                
                return -1;
                break;
            }
            printf("- SI\n");
            printf("- Controllo che l'utente sia registrato\n");

            int hashed_lock_found=hashing(rice.data.hdr.receiver)%32;
            pthread_mutex_lock(&lock_userHashTable[hashed_lock_found]);
            found=HashFind(rice.data.hdr.receiver,userHashTable);
            if(found==NULL){
                //utente non registrato invio messaggio di errore
                pthread_mutex_unlock(&lock_userHashTable[hashed_lock_found]);
                memset(answer,0,sizeof(message_t)); //free
                setHeader(&(answer->hdr),OP_NICK_UNKNOWN,rice.data.hdr.receiver);
                sendHeader(chan,&(answer->hdr));
                free(answer);
                free(toRec);

                //aggiornamento stat
                pthread_mutex_lock(&lock_stat);
                chattyStats.nerrors++;
                pthread_mutex_unlock(&lock_stat);
                //------------------------------

                return -1;
                break;
           }
           else if(found->isOn==1 && found->isGroup==0){ //online e non è un gruppo
                printf("- STO PER INVIARE A: %s\n",found->nickname);
                printf("- SUL SOCKET: %d\n",found->channel);

                setHeader(&(toRec->hdr),TXT_MESSAGE,rice.hdr.sender);
                setData(&(toRec->data),rice.data.hdr.receiver,rice.data.buf,rice.data.hdr.len*sizeof(char));

                //invio il messaggio di testo al destinatario
                sendHeader(found->channel,&(toRec->hdr));
                sendData(found->channel,&(toRec->data));

                pthread_mutex_unlock(&lock_userHashTable[hashed_lock_found]); //UNLOCK

                //invio OP_OK al mittente
                memset(answer,0,sizeof(message_t)); //free
                setHeader(&(answer->hdr),OP_OK,rice.data.hdr.receiver);
                sendHeader(chan,&(answer->hdr));
                free(answer);
                free(toRec);

                //aggiornamento stat
                pthread_mutex_lock(&lock_stat);
                chattyStats.ndelivered++;
                pthread_mutex_unlock(&lock_stat);
                //------------------------------

                return 0;
                break;
           }
           else if(found->isOn==0 && found->isGroup==0){ //offline e non è un gruppo
                //preparo il messaggio
                setHeader(&(toRec->hdr),TXT_MESSAGE,rice.hdr.sender);
                setData(&(toRec->data),rice.data.hdr.receiver,rice.data.buf,rice.data.hdr.len*sizeof(char));
                //lo inserisco nella sua history
                found->ricevuti=addMess(found->ricevuti,chan,toRec);
                found->toR++;
                //se il numero di messaggi nella sua history è superiore al limite, tolgo il più vecchio
                if(found->toR>MaxHistMsgs){
                    free(found->ricevuti->received->data.buf);
                    free(found->ricevuti->received);
                    found->ricevuti=removeMess(found->ricevuti);
                    found->toR--;
                }
                pthread_mutex_unlock(&lock_userHashTable[hashed_lock_found]);
                memset(answer,0,sizeof(message_t)); 
                setHeader(&(answer->hdr),OP_OK,rice.data.hdr.receiver);
                sendHeader(chan,&(answer->hdr));
                free(answer);
                free(toRec);

                //aggiornamento stat
                pthread_mutex_lock(&lock_stat);
                chattyStats.nnotdelivered++;
                pthread_mutex_unlock(&lock_stat);
                //------------------------------

                return 0;
                break;
           }
           //PARTE SUI GRUPPI
           else if (found->isGroup==1){
               printf("e' un gruppo\n");
               int sender_lock_index=hashing(rice.hdr.sender)%32;
               if(sender_lock_index!=hashed_lock_found) pthread_mutex_lock(&lock_userHashTable[sender_lock_index]); // evito di premdere due volte la stessa lock
               HashElem* Sender=HashFind(rice.hdr.sender,userHashTable);
               int a=isInGroup(rice.data.hdr.receiver,Sender->isIn); // controllo che chi spedisca il messaggio sia all'interno del gruppo
               if(a!=0){

                    if(sender_lock_index!=hashed_lock_found) pthread_mutex_unlock(&lock_userHashTable[sender_lock_index]); //evito di rilasciare due volte la stessa lock
                    pthread_mutex_unlock(&lock_userHashTable[hashed_lock_found]);
                    memset(answer,0,sizeof(message_t)); //free
                    setHeader(&(answer->hdr),OP_NICK_UNKNOWN,rice.hdr.sender);
                    sendHeader(chan,&(answer->hdr));
                    free(answer);
                    free(toRec);

                    //aggiornamento stat
                    pthread_mutex_lock(&lock_stat);
                    chattyStats.nerrors++;
                    pthread_mutex_unlock(&lock_stat);
                    //------------------------------

                    return -1;
                    break;
               }
               if(sender_lock_index!=hashed_lock_found) pthread_mutex_unlock(&lock_userHashTable[sender_lock_index]); // evito di rilasciare due volte la stessa lock
               LinkEl* aux=NULL;
               aux=found->GroupList;
               while(aux!=NULL && aux->memberLink!=NULL){
                   int lock_receiver_index=hashing(aux->memberName)%32;

                   if(lock_receiver_index!=hashed_lock_found){
                       printf("- LOCK PRESA\n");
                       pthread_mutex_lock(&lock_userHashTable[lock_receiver_index]);
                   } 
                   printf("- Invio a %s\n",aux->memberName);
                   //invio a tutti i membri del gruppo
                   if(aux->memberLink->isOn==1){ // membro del grupo online
                        setHeader(&(toRec->hdr),TXT_MESSAGE,rice.hdr.sender);
                        setData(&(toRec->data),rice.data.hdr.receiver,rice.data.buf,rice.data.hdr.len*sizeof(char));
                        sendHeader(aux->memberLink->channel,&(toRec->hdr));
                        sendData(aux->memberLink->channel,&(toRec->data));

                        //aggiornamento stat
                        pthread_mutex_lock(&lock_stat);
                        chattyStats.ndelivered++;
                        pthread_mutex_unlock(&lock_stat);
                        //------------------------------

                   }
                   else if(aux->memberLink->isOn==0 ){ // membro del gruppo offline
                        setHeader(&(toRec->hdr),TXT_MESSAGE,rice.hdr.sender);
                        setData(&(toRec->data),rice.data.hdr.receiver,rice.data.buf,rice.data.hdr.len*sizeof(char));
                        aux->memberLink->ricevuti=addMess(aux->memberLink->ricevuti,chan,toRec);
                        aux->memberLink->toR++;
                        //controllo le history dei riceventi e nel caso elimino il messaggio più vecchio
                        if(aux->memberLink->toR>MaxHistMsgs){
                            free(aux->memberLink->ricevuti->received->data.buf);
                            free(aux->memberLink->ricevuti->received);
                            aux->memberLink->ricevuti=removeMess(aux->memberLink->ricevuti);
                            aux->memberLink->toR--;
                        }

                        //aggiornamento stat
                        pthread_mutex_lock(&lock_stat);
                        chattyStats.nnotdelivered++;
                        pthread_mutex_unlock(&lock_stat);
                        //------------------------------
                   }
                   if(lock_receiver_index!=hashed_lock_found){pthread_mutex_unlock(&lock_userHashTable[lock_receiver_index]);}

                   aux=aux->next;
                }
                pthread_mutex_unlock(&lock_userHashTable[hashed_lock_found]);
                pthread_mutex_unlock(&lock_userHashTable[hashed_lock]);
                memset(answer,0,sizeof(message_t)); //free ??????
                setHeader(&(answer->hdr),OP_OK,rice.data.hdr.receiver);
                sendHeader(chan,&(answer->hdr));
                free(answer);
                free(toRec);
                return 0;
                break;
           }
           // errore generico
           pthread_mutex_unlock(&lock_userHashTable[hashed_lock_found]);
           pthread_mutex_unlock(&lock_userHashTable[hashed_lock]);
           memset(answer,0,sizeof(message_t)); //free
           setHeader(&(answer->hdr),OP_FAIL,rice.hdr.sender);
           sendHeader(chan,&(answer->hdr));
           free(answer);
           free(toRec);

           //aggiornamento stat
           pthread_mutex_lock(&lock_stat);
           chattyStats.nerrors++;
           pthread_mutex_unlock(&lock_stat);
           //------------------------------

           return -1;
           break;

        case POSTTXTALL_OP:
            //invia lo stesso messaggio testuale a tutti gli utenti registrati
            printf("------POSTTXTALL_OP------\n");
            int j=0;
            //preparo il messaggio solo una volta
            memset(toRec,0,sizeof(message_t));
            setHeader(&(toRec->hdr),TXT_MESSAGE,rice.hdr.sender);
            setData(&(toRec->data),"",rice.data.buf,rice.data.hdr.len*sizeof(char));

            //scorro l'inera tabella hash
            for(j=0;j<1024;j++){
                pthread_mutex_lock(&lock_userHashTable[j%32]);
                if(userHashTable[j]!=NULL){
                    HashElem* aux=userHashTable[j];
                    while(aux!=NULL){
                        printf("- Invio a tutti , tocca a %s\n",aux->nickname);
                        if(aux->isOn==0 && aux->isGroup==0){ //offline
                            aux->ricevuti=addMess(aux->ricevuti,chan,toRec);
                            aux->toR++;

                            if(aux->toR>MaxHistMsgs){ 
                                free(aux->ricevuti->received->data.buf);
                                free(aux->ricevuti->received);
                                aux->ricevuti=removeMess(aux->ricevuti);
                                aux->toR--;
                            }

                            //aggiornamento stat
                            pthread_mutex_lock(&lock_stat);
                            chattyStats.nnotdelivered++;
                            pthread_mutex_unlock(&lock_stat);
                            //------------------------------

                        }
                        else if(aux->isOn==1 && aux->isGroup==0){ // online
                            sendHeader(aux->channel,&(toRec->hdr));
                            sendData(aux->channel,&(toRec->data));

                            //aggiornamento stat
                            pthread_mutex_lock(&lock_stat);
                            chattyStats.ndelivered++;
                            pthread_mutex_unlock(&lock_stat);
                            //------------------------------
                        }
                        aux=aux->next;
                    }
                }
                pthread_mutex_unlock(&lock_userHashTable[j%32]);
            }
            memset(answer,0,sizeof(message_t));
            setHeader(&(answer->hdr),OP_OK,"Server");
            sendHeader(chan,&(answer->hdr));
            free(answer);
            free(toRec);
            printf("-------FINE POSTTXTALL_OP-------\n");
            return 0;
            break;
        
        case POSTFILE_OP:
            //invio di un file
            printf("------POSTFILE_OP------\n");
            readData(chan,&fileRice); //GLI ARRIVA PROPRIO IL FILE
            printf("- lunghezza file da mandare:%d e Max è %d\n",fileRice.hdr.len,MaxFileSize*1024);
            //controllo che il file non sia troppo grande
            if(fileRice.hdr.len>MaxFileSize*1024){
                printf("- ERRORE: File troppo grande\n");
                memset(answer,0,sizeof(message_t)); //free
                setHeader(&(answer->hdr),OP_MSG_TOOLONG,rice.data.hdr.receiver);
                sendHeader(chan,&(answer->hdr));
                free(fileRice.buf);
                free(answer);
                free(toRec);

                //aggiornamento stat
                pthread_mutex_lock(&lock_stat);
                chattyStats.nerrors++;
                pthread_mutex_unlock(&lock_stat);
                //------------------------------

                return -1;
                break;
            }
             
            //Estraggo il nome del file da copiare in modo da trovarlo più avanti
            int dimensione=strlen(rice.data.buf); //dimensione totale della stringa
            
            int indice=dimensione;
            printf("- IL FILE DA PRENDERE è %s\n",rice.data.buf);
            while(rice.data.buf[indice]!='/' && indice!=0){  //la percorro al contrario finchè non trovo '/'
                indice--;
            }
            
            char* nomeInput=calloc((dimensione-indice),sizeof(char)); //a questo punto so la lunghezza del nome (dimensione-indice)
            strncpy(nomeInput,&rice.data.buf[indice],dimensione-indice); //copio il nome dalla posizione dalla cui inizia nella stringa originale
            int lungDirName=(int)strlen(DirName);
            char* pathCompleto=malloc((lungDirName+(dimensione-indice)+2)*sizeof(char)); // sommo le dimensioni 
            strcpy(pathCompleto,DirName);
            if(rice.data.buf[indice]!='/') strncat(pathCompleto,"/",1); //se manca '/' lo aggiungo
            strncat(pathCompleto,nomeInput,dimensione-indice); //concateno le stringhe
        

            printf("- CARTELLA:%s\n",pathCompleto);
            printf("- DIRNAME:%s\n",DirName);
           
            //apro il file dove "riversare" i dati che ho ricevuto
            FILE* output = fopen(pathCompleto,"wb");
            if(errno==2){ // se la cartella destinazione non esiste, la creo e riprovo
                    printf("CREO DIRNAME\n");
                    mkdir(DirName,0777);
                    printf("RIPROVO\n");
                    output = fopen(pathCompleto,"wb");
            }
            else if(errno!=0 && errno!=2){ // in caso di altri problemi segnalo l'errore ed esco
                printf("%d\n",errno);
                perror("Apertura file output");
                return -1;

            }
            //scrivo nel file aperto (creato)
            if (fwrite(fileRice.buf, sizeof(char), fileRice.hdr.len, output)!=fileRice.hdr.len){
                perror("Errore scrittura file");
                fclose(output);
                return -1;
            }
            fclose(output);
            free(nomeInput);
            //controllo che il ricevente esista
            index_lock_receiver=hashing(rice.data.hdr.receiver)%32;
            pthread_mutex_lock(&lock_userHashTable[index_lock_receiver]);
            found=HashFind(rice.data.hdr.receiver,userHashTable);
            if(found==NULL){
                pthread_mutex_unlock(&lock_userHashTable[index_lock_receiver]);
                memset(answer,0,sizeof(message_t)); //free
                setHeader(&(answer->hdr),OP_FAIL,rice.hdr.sender);
                sendHeader(chan,&(answer->hdr));
                free(fileRice.buf);
                free(pathCompleto);
                free(answer);
                free(toRec);

                //aggiornamento stat
                pthread_mutex_lock(&lock_stat);
                chattyStats.nerrors++;
                pthread_mutex_unlock(&lock_stat);
                //------------------------------

                return -1;
                break;
            }
            if(found->isOn==1 && found->isGroup==0){ // online e non è un gruppo
                //gli invio il path del file in cui risiede il file che gli hanno inviato
                setHeader(&(toRec->hdr),FILE_MESSAGE,rice.hdr.sender);
                setData(&(toRec->data),rice.data.hdr.receiver,pathCompleto,strlen(pathCompleto)*sizeof(char));
                sendHeader(found->channel,&(toRec->hdr));
                sendData(found->channel,&(toRec->data));
                pthread_mutex_unlock(&lock_userHashTable[index_lock_receiver]);
                memset(answer,0,sizeof(message_t)); //free
                //invio OP_OK al mittente
                setHeader(&(answer->hdr),OP_OK,rice.data.hdr.receiver);
                sendHeader(chan,&(answer->hdr));
                free(fileRice.buf);
                free(pathCompleto);
                free(answer);
                free(toRec);


                //aggiornamento stat
                pthread_mutex_lock(&lock_stat);
                chattyStats.nfiledelivered++;
                pthread_mutex_unlock(&lock_stat);
                //------------------------------

                return 0;
                break;
            }
            if(found->isOn==0 && found->isGroup==0){ // offline e non è un gruppo
                //gli invio il path del file in cui risiede il file che gli hanno inviato, e lo salvo nella sua history
                setHeader(&(toRec->hdr),FILE_MESSAGE,rice.hdr.sender);
                setData(&(toRec->data),rice.data.hdr.receiver,pathCompleto,strlen(pathCompleto)*sizeof(char));
                found->ricevuti=addMess(found->ricevuti,chan,toRec);
                found->toR++;

                if(found->toR>MaxHistMsgs){ //controllo history
                    free(found->ricevuti->received->data.buf);
                    free(found->ricevuti->received);
                    found->ricevuti=removeMess(found->ricevuti);
                    found->toR--;
                }

                //OP_OK la mittente
                pthread_mutex_unlock(&lock_userHashTable[index_lock_receiver]);
                memset(answer,0,sizeof(message_t)); //free
                setHeader(&(answer->hdr),OP_OK,rice.data.hdr.receiver);
                sendHeader(chan,&(answer->hdr));
                free(fileRice.buf);
                free(pathCompleto);
                free(answer);
                free(toRec);

                //aggiornamento stat
                pthread_mutex_lock(&lock_stat);
                chattyStats.nfilenotdelivered++;
                pthread_mutex_unlock(&lock_stat);
                //------------------------------

                return 0;
                break;
            }
            //GRUPPO
            if(found->isGroup==1){
                
                HashElem* Sender=HashFind(rice.hdr.sender,userHashTable);

                //il mittente deve essere nel gruppo (guardare POSTTXT_OP)
                if(index_lock_receiver!=hashed_lock) pthread_mutex_lock(&lock_userHashTable[hashed_lock]);
                int a=isInGroup(rice.data.hdr.receiver,Sender->isIn);
                if(index_lock_receiver!=hashed_lock) pthread_mutex_unlock(&lock_userHashTable[hashed_lock]);
                
                if(a!=0){
                    pthread_mutex_unlock(&lock_userHashTable[index_lock_receiver]);
                    memset(answer,0,sizeof(message_t));
                    setHeader(&(answer->hdr),OP_NICK_UNKNOWN,rice.hdr.sender);
                    sendHeader(chan,&(answer->hdr));
                    free(answer);
                    free(toRec);

                    //aggiornamento stat
                    pthread_mutex_lock(&lock_stat);
                    chattyStats.nerrors++;
                    pthread_mutex_unlock(&lock_stat);
                    //------------------------------

                    return -1;
                    break;
                }
                LinkEl* aux=NULL;
                aux=found->GroupList;
                while(aux!=NULL){
                    int lock_aux_memberLink=hashing(aux->memberName)%32;
                    if(index_lock_receiver!=lock_aux_memberLink) pthread_mutex_lock(&lock_userHashTable[lock_aux_memberLink]); //evito la lock due volte
                    if(aux->memberLink->isOn==1){ //online
                        setHeader(&(toRec->hdr),FILE_MESSAGE,rice.hdr.sender);
                        setData(&(toRec->data),rice.data.hdr.receiver,pathCompleto,strlen(pathCompleto)*sizeof(char));
                        sendHeader(aux->memberLink->channel,&(toRec->hdr));
                        sendData(aux->memberLink->channel,&(toRec->data));

                        //aggiornamento stat
                        pthread_mutex_lock(&lock_stat);
                        chattyStats.nfiledelivered++;
                        pthread_mutex_unlock(&lock_stat);
                        //------------------------------
                    }
                    else if(aux->memberLink->isOn==0){ //offline
                        setHeader(&(toRec->hdr),FILE_MESSAGE,rice.hdr.sender);
                        setData(&(toRec->data),rice.data.hdr.receiver,pathCompleto,strlen(pathCompleto)*sizeof(char));
                        aux->memberLink->ricevuti=addMess(aux->memberLink->ricevuti,chan,toRec);
                        aux->memberLink->toR++;

                        if(aux->memberLink->toR>MaxHistMsgs){
                            free(aux->memberLink->ricevuti->received->data.buf);
                            free(aux->memberLink->ricevuti->received);
                            aux->memberLink->ricevuti=removeMess(aux->memberLink->ricevuti);
                            aux->memberLink->toR--;
                        }

                        //aggiornamento stat
                        pthread_mutex_lock(&lock_stat);
                        chattyStats.nfilenotdelivered++;
                        pthread_mutex_unlock(&lock_stat);
                        //------------------------------

                    }
                    if(index_lock_receiver!=lock_aux_memberLink) pthread_mutex_unlock(&lock_userHashTable[lock_aux_memberLink]); //evito doppia unlock
                    aux=aux->next;
                }
                pthread_mutex_unlock(&lock_userHashTable[index_lock_receiver]);
                memset(answer,0,sizeof(message_t));
                setHeader(&(answer->hdr),OP_OK,rice.data.hdr.receiver);
                sendHeader(chan,&(answer->hdr));
                free(fileRice.buf);
                free(pathCompleto);
                free(answer);
                free(toRec);
                return 0;
                break;
            }
            //fallimento
            pthread_mutex_unlock(&lock_userHashTable[index_lock_receiver]);
            memset(answer,0,sizeof(message_t)); //free
            setHeader(&(answer->hdr),OP_FAIL,rice.hdr.sender);
            sendHeader(chan,&(answer->hdr));
            free(fileRice.buf);
            free(pathCompleto);
            free(answer);
            free(toRec);

            //aggiornamento stat
            pthread_mutex_lock(&lock_stat);
            chattyStats.nerrors++;
            pthread_mutex_unlock(&lock_stat);
            //------------------------------

            return -1;
            break;


        case GETFILE_OP:
            //recupero di un file dalla "memoria" del server

            printf("------GETFILE_OP------\n");
            printf("- PATH ricevuto da getfile %s\n",rice.data.buf);
            errno=0;
            //apro il file che devo inviare
            inFile = fopen(rice.data.buf,"r");
            if(errno!=0){
                perror("Apertura file da copiare");
                errno=0;
                memset(answer,0,sizeof(message_t)); //free
                setHeader(&(answer->hdr),OP_NO_SUCH_FILE,rice.data.hdr.receiver);
                sendHeader(chan,&(answer->hdr));
                free(answer);
                free(toRec);

                //aggiornamento stat
                pthread_mutex_lock(&lock_stat);
                chattyStats.nerrors++;
                pthread_mutex_unlock(&lock_stat);
                //------------------------------

                return -1;
                break;
            }
            
            //conversione byte del file in char
            char* fileInChar=NULL; // alloco la stringa che conterra il file convertito
            fseek(inFile,0,SEEK_END); // vado in fondo al file
            int dimFile=ftell(inFile); // e mi prendo la dimensione
            fileInChar=calloc((dimFile+1),sizeof(char)); // alloco la stringa della dimensione richiesta
            fseek(inFile,0,SEEK_SET); // torno all'inizio del file 
            fread(fileInChar,dimFile,1,inFile); // scrivo 1 byte per volta nella stringa
            fclose(inFile); // chiudo il file 

            if(errno!=0){ //controllo che sia andato tutto bene
                perror("Conversione file in char");
                errno=0;
                return -1;
            }

            
            memset(answer,0,sizeof(message_t)); 
            setHeader(&(answer->hdr),OP_OK,rice.data.hdr.receiver);
            setData(&(answer->data),rice.data.hdr.receiver,fileInChar,(dimFile+1)*sizeof(char));


            sendHeader(chan,&(answer->hdr));
            sendData(chan,&(answer->data));

            free(fileInChar); // libero la stringa contenente il file 
            free(answer);
            free(toRec);
            printf("------FINE GETFILE_OP-----\n");

            return 0;
            break;
        
        case GETPREVMSGS_OP:
            //recupero messaggi dalla history

            printf("------GETPREVMSGS_OP------\n");
            printf("- chiedo la lock numero %d\n",hashed_lock);
            pthread_mutex_lock(&lock_userHashTable[hashed_lock]);
            printf("- controllo che %s sia registrato\n",rice.hdr.sender);
            found=HashFind(rice.hdr.sender,userHashTable);
            if(found==NULL){
                printf("- %s non è registrato\n",rice.hdr.sender);
                pthread_mutex_unlock(&lock_userHashTable[hashed_lock]);
                memset(answer,0,sizeof(message_t)); 
                setHeader(&(answer->hdr),OP_FAIL,rice.data.hdr.receiver);
                sendHeader(chan,&(answer->hdr));
                free(answer);
                free(toRec);


                //aggiornamento stat
                pthread_mutex_lock(&lock_stat);
                chattyStats.nerrors++;
                pthread_mutex_unlock(&lock_stat);
                //------------------------------

                return -1;
                break;
            }
            printf("- Controllo se %s ha dei messaggi\n",rice.hdr.sender);
            messRec* auxMess=found->ricevuti;
            if(auxMess==NULL){
                printf("- ERRORE: non ne ha\n");
                printf("- lascio la lock numero %d\n",hashed_lock);
                pthread_mutex_unlock(&lock_userHashTable[hashed_lock]);
                memset(answer,0,sizeof(message_t)); //free
                setHeader(&(answer->hdr),OP_FAIL,rice.data.hdr.receiver);
                sendHeader(chan,&(answer->hdr));
                free(answer);
                free(toRec);

                //aggiornamento stat
                pthread_mutex_lock(&lock_stat);
                chattyStats.nerrors++;
                pthread_mutex_unlock(&lock_stat);
                //------------------------------

                return -1;
                break;
            }
            printf("- ne ha %d\n",auxMess->num);
            memset(answer,0,sizeof(message_t)); //free
            setHeader(&(answer->hdr),OP_OK,rice.data.hdr.receiver);
            sendHeader(chan,&(answer->hdr));
            //conversione in char del numero dei messaggi pendenti
            setData(&(answer->data),"",(char*)&auxMess->num,sizeof(size_t));
            sendData(chan,&(answer->data));
            message_t* messExtracted;
            
            while(auxMess!=NULL){
                // invio messaggi fino a quando non li ho inviati tutti
                messExtracted=auxMess->received;
                sendHeader(chan,&(messExtracted->hdr));
                sendData(chan,&(messExtracted->data));

                //aggiornamento stat
                pthread_mutex_lock(&lock_stat);
                if(messExtracted->hdr.op==TXT_MESSAGE){
                    chattyStats.nnotdelivered--;
                    chattyStats.ndelivered++;
                }
                if(messExtracted->hdr.op==FILE_MESSAGE){
                    chattyStats.nfilenotdelivered--;
                    chattyStats.nfiledelivered++;
                }
                pthread_mutex_unlock(&lock_stat);
                //------------------------------


                auxMess=auxMess->next; //e scorro

                


                
            }
            printf("- lascio la lock numero %d\n",hashed_lock);
            pthread_mutex_unlock(&lock_userHashTable[hashed_lock]);
            
            free(answer);
            free(toRec);
            printf("------FINE GETPREVMS_OP------\n");
            return 0;
            break;

        case DISCONNECT_OP:
            //disconnessione utente
            printf("------DISCONNECT_OP-----\n");
            pthread_mutex_lock(&lock_userHashTable[hashed_lock]);
            found=HashFind(rice.hdr.sender,userHashTable);
            if(found!=NULL){ //lo disconetto
                found->isOn=0;
                found->channel=0;
                pthread_mutex_lock(&lock_lista_ON);
                lista_ON=removeGroupMem(lista_ON,rice.hdr.sender);
                nusersON--;
                pthread_mutex_unlock(&lock_lista_ON);

                //aggiornamento stat
                pthread_mutex_lock(&lock_stat);
                chattyStats.nonline--;
                pthread_mutex_unlock(&lock_stat);
                //------------------------------

                
            }
            else {
                pthread_mutex_unlock(&lock_userHashTable[hashed_lock]);
                memset(answer,0,sizeof(message_t)); //free
                setHeader(&(answer->hdr),OP_NICK_UNKNOWN,rice.data.hdr.receiver);
                sendHeader(chan,&(answer->hdr));
                free(answer);
                free(toRec);

                //aggiornamento stat
                pthread_mutex_lock(&lock_stat);
                chattyStats.nerrors++;
                pthread_mutex_unlock(&lock_stat);
                //------------------------------

                return -1;
                break;
            }
            pthread_mutex_unlock(&lock_userHashTable[hashed_lock]);
            memset(answer,0,sizeof(message_t)); //free
            setHeader(&(answer->hdr),OP_OK,rice.data.hdr.receiver);
            sendHeader(chan,&(answer->hdr));
            free(answer);
            free(toRec);
            printf("-----FINE DISCONNECT_OP-----\n");
            return 0;
            break;

        case UNREGISTER_OP:
            printf("------UNREGISTER_OP------\n");
            esito=0;
            index=hashing(rice.data.hdr.receiver); //
            hashed_lock_found=index%32;
            pthread_mutex_lock(&lock_userHashTable[hashed_lock_found]);

            printf("- Controllo se %s è registrato\n",rice.data.hdr.receiver); 
            if(userHashTable[index]==NULL){
                esito=-1;
            }
            else if(strcmp(userHashTable[index]->nickname,rice.data.hdr.receiver)==0){
                printf("- E' il primo elemento della lista all'indice %d\n",index);
                HashElem* aux=userHashTable[index];
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
                    stringList* aux2=aux->isIn;
                    int hashed_lock_isIn=hashing(aux->isIn->elem)%32;
                    if(hashed_lock_isIn!=hashed_lock_found) pthread_mutex_lock(&lock_userHashTable[hashed_lock_isIn]);
                    HashElem* found=HashFind(aux->isIn->elem,userHashTable);
                    printf("- Lo elimino da %s\n",aux->isIn->elem);
                    found->GroupList=removeGroupMem(found->GroupList,rice.data.hdr.receiver);
                    free(aux->isIn->elem);
                    aux->isIn=aux->isIn->next;
                    if(hashed_lock_isIn!=hashed_lock_found) pthread_mutex_unlock(&lock_userHashTable[hashed_lock_isIn]);
                    free(aux2);
                }
                free(aux->nickname);
                userHashTable[index]=userHashTable[index]->next;
                free(aux);
            }



            pthread_mutex_unlock(&lock_userHashTable[hashed_lock_found]);
            if(esito<0){
                printf("ERRORE: Utente da deregistrare non esistente\n");
                memset(answer,0,sizeof(message_t)); //free
                setHeader(&(answer->hdr),OP_NICK_UNKNOWN,rice.data.hdr.receiver);
                sendHeader(chan,&(answer->hdr));
                free(answer);
                free(toRec);

                //aggiornamento stat
                pthread_mutex_lock(&lock_stat);
                chattyStats.nerrors++;
                pthread_mutex_unlock(&lock_stat);
                //------------------------------

                return -1;
                break;
            }
            //se devo disconnettere il sender, aggiorno subito la lista degli utenti online
            // per evitare che riceva messaggi mentre è nel limbo (è disconesso ma risulta connesso)
            if(strcmp(rice.hdr.sender,rice.data.hdr.receiver)==0){ 
                pthread_mutex_lock(&lock_lista_ON);
                lista_ON=removeGroupMem(lista_ON,rice.hdr.sender);
                nusersON--;
                pthread_mutex_unlock(&lock_lista_ON);


                //aggiornamento stat
                pthread_mutex_lock(&lock_stat);
                chattyStats.nonline--;
                pthread_mutex_unlock(&lock_stat);
                //------------------------------

            }
            memset(answer,0,sizeof(message_t)); //free
            setHeader(&(answer->hdr),OP_OK,rice.data.hdr.receiver);
            sendHeader(chan,&(answer->hdr));
            free(answer);
            free(toRec);

            //aggiornamento stat
            pthread_mutex_lock(&lock_stat);
            chattyStats.nusers--;
            pthread_mutex_unlock(&lock_stat);
            //------------------------------

            printf("------FINE UNREGISTER_OP------\n");
            return 0;
            break;

        case CREATEGROUP_OP:
            //crea un gruppo e lo aggiunge alla tabellahash

            printf("------CREATEGROUP_OP-----\n");
            printf("- Controllo se il nome assegnato al gruppo è già presente nella tabella\n");
            index=hashing(rice.data.hdr.receiver);
            hashed_lock_found=index%32;
            pthread_mutex_lock(&lock_userHashTable[hashed_lock_found]);
            found=HashFind(rice.data.hdr.receiver,userHashTable);
            if(found!=NULL){
                printf("- ERRORE: sì, è già presente e non posso procedere.\n");
                pthread_mutex_unlock(&lock_userHashTable[hashed_lock_found]);
                //richiesta fallita ed esco
                memset(answer,0,sizeof(message_t)); //free
                setHeader(&(answer->hdr),OP_NICK_ALREADY,"");
                sendHeader(chan,&(answer->hdr));
                free(answer);
                free(toRec); 
                return -1;
                break;
            }
            else{
                printf("- No,posso aggiungerlo\n");
                userHashTable[index]=aggiuntaElem(rice.data.hdr.receiver,0,userHashTable[index]);
                found=HashFind(rice.data.hdr.receiver,userHashTable);
                if(found!=NULL){
                    found->admin=calloc(MAX_NAME_LENGTH+1,sizeof(char));
                    strcpy(found->admin,rice.hdr.sender);
                    found->isGroup=1;
                    if(hashed_lock_found!=hashed_lock) pthread_mutex_lock(&lock_userHashTable[hashed_lock]);
                    HashElem* toAdd=HashFind(rice.hdr.sender,userHashTable);
                    //aggiungo il creatore al gruppo
                    found->GroupList=addLinkMem(found->GroupList,toAdd); 
                    toAdd->isIn=addUserOn(rice.data.hdr.receiver,toAdd->isIn);
                    if(hashed_lock_found!=hashed_lock) pthread_mutex_unlock(&lock_userHashTable[hashed_lock]);
                    printf("- Creazione avvenuta con successo!\n");
                }
                else{
                    printf("- ERRORE: qualcosa è andato male\n");
                    pthread_mutex_unlock(&lock_userHashTable[hashed_lock_found]);
                    //fallimento
                    memset(answer,0,sizeof(message_t)); //free
                    setHeader(&(answer->hdr),OP_FAIL,"");
                    sendHeader(chan,&(answer->hdr));
                    free(answer);
                    free(toRec);  
                    return -1;
                    break;
                }
            }
            //OP_OK
            pthread_mutex_unlock(&lock_userHashTable[hashed_lock_found]);
            memset(answer,0,sizeof(message_t)); //free
            setHeader(&(answer->hdr),OP_OK,"");
            sendHeader(chan,&(answer->hdr));
            free(answer);
            free(toRec);
            printf("-----FINE CREATEGROUP_OP-----\n");
            perror("FINE CREATEGROUP");
            return 0;
            break;

        case ADDGROUP_OP:
            //aggiunta di un membro ad un gruppo

            printf("-----ADDGROUP_OP-----\n");
            hashed_lock_found=hashing(rice.data.hdr.receiver)%32;
            pthread_mutex_lock(&lock_userHashTable[hashed_lock_found]);
            found=HashFind(rice.data.hdr.receiver,userHashTable);
            if(found==NULL || found->isGroup!=1){
                pthread_mutex_unlock(&lock_userHashTable[hashed_lock_found]);
                printf("NON è un gruppo\n");
                memset(answer,0,sizeof(message_t)); //free
                setHeader(&(answer->hdr),OP_FAIL,"");
                sendHeader(chan,&(answer->hdr));
                free(answer);
                free(toRec);
                return -1;
                break;
            }
            else{
                //aggiungo l'utente se il gruppo esiste 
                if(hashed_lock!=hashed_lock_found) pthread_mutex_lock(&lock_userHashTable[hashed_lock]);
                HashElem* toAdd=HashFind(rice.hdr.sender,userHashTable);
                found->GroupList=addLinkMem(found->GroupList,toAdd);
                toAdd->isIn=addUserOn(rice.data.hdr.receiver,toAdd->isIn);
                if(hashed_lock!=hashed_lock_found) pthread_mutex_unlock(&lock_userHashTable[hashed_lock]);
            }
            pthread_mutex_unlock(&lock_userHashTable[hashed_lock_found]);
            memset(answer,0,sizeof(message_t)); //free
            setHeader(&(answer->hdr),OP_OK,"");
            sendHeader(chan,&(answer->hdr));
            free(answer);
            free(toRec);
            printf("-----FINE ADDGROUP----\n");
            return 0;
            break;
        
        case DELGROUP_OP:
            printf("-----DELGROUP_OP-----\n");
            //rimozione di un utente da un gruppo
            
            hashed_lock_found=hashing(rice.data.hdr.receiver)%32;
            pthread_mutex_lock(&lock_userHashTable[hashed_lock_found]);
            found=HashFind(rice.data.hdr.receiver,userHashTable);
            if(found==NULL || found->isGroup!=1){
                pthread_mutex_unlock(&lock_userHashTable[hashed_lock_found]);
                memset(answer,0,sizeof(message_t)); 
                setHeader(&(answer->hdr),OP_FAIL,"");
                sendHeader(chan,&(answer->hdr));
                free(answer);
                free(toRec);
                return -1;
                break;
            }
            else{
                if(hashed_lock!=hashed_lock_found) pthread_mutex_lock(&lock_userHashTable[hashed_lock]);
                HashElem* toAdd=HashFind(rice.hdr.sender,userHashTable);
                found->GroupList=removeGroupMem(found->GroupList,rice.hdr.sender);
                toAdd->isIn=removeString(rice.data.hdr.receiver,toAdd->isIn);
                if(hashed_lock!=hashed_lock_found) pthread_mutex_unlock(&lock_userHashTable[hashed_lock]);
            }
            pthread_mutex_unlock(&lock_userHashTable[hashed_lock_found]);
            memset(answer,0,sizeof(message_t)); 
            setHeader(&(answer->hdr),OP_OK,"");
            sendHeader(chan,&(answer->hdr));
            free(answer);
            free(toRec);
            printf("-----FINE DELGROUP----\n");
            return 0;
            break;
        default: return -1;
    } 
    return -1;
}


//Funzione principale che esegue ogni worker
void* threadOp(){
    message_t* messExtracted;
    int esito=-1;
    while(!clean_bit){ // questo valore viene settato dal segnale di chiusura

        pthread_mutex_lock(&lock_fd_queue);
        //estraggo dalla coda un fd 
        int fdExtracted=PullQueue(fd_queue);
        printf("Ho estratto: %d\n",fdExtracted);
        while(fdExtracted<0){
            //PullQueue restituisce -1 quando la coda è vuota
            //se è questo il caso aspetto che si riempia
            pthread_cond_wait(&cond_fd_queue_Pull,&lock_fd_queue);
            fdExtracted=PullQueue(fd_queue);
            if(clean_bit){ //se mi sveglio e il bit di chiusura è settato allora esco
                pthread_mutex_unlock(&lock_fd_queue);
                return 0;
            } 
        }
        pthread_cond_signal(&cond_fd_queue_Push); //mando un segnale in caso qualcuno stia aspettando un posto nella coda
        pthread_mutex_unlock(&lock_fd_queue);

        messExtracted=calloc(1,sizeof(message_t));
        //leggo un messaggio dal fd
        int letto=readMsg(fdExtracted,messExtracted);
        if(letto>0){
            esito=worker(*messExtracted,fdExtracted);
            if(esito==0){ //la funzione worker è terminata correttamente
                //c'è ancora qualcosa da leggere rimetto il fd nel set
            
                pthread_mutex_lock(&lock_set);
                FD_SET(fdExtracted,&set);
                pthread_mutex_unlock(&lock_set);
            }
            else{
                //disconnessione
                pthread_mutex_lock(&lock_lista_ON);
                //cerco l'utente da disconnettere in base al suo fd
                HashElem* toDis=listaONFind(fdExtracted,lista_ON);
                int toDisIndex=0;
                if(toDis!=NULL && toDis->isOn==1){
                    //lo disconetto
                    toDisIndex=hashing(toDis->nickname)%32;
                    pthread_mutex_lock(&lock_userHashTable[toDisIndex]);
                    toDis->isOn=0;
                    toDis->channel=-1;
                    lista_ON=removeGroupMem(lista_ON,toDis->nickname);
                    nusersON--;
                    pthread_mutex_unlock(&lock_userHashTable[toDisIndex]);

                    //aggiornamento stat
                    pthread_mutex_lock(&lock_stat);
                    chattyStats.nonline--;
                    pthread_mutex_unlock(&lock_stat);
                    //------------------------------


                }
                pthread_mutex_lock(&lock_set);
                    FD_SET(fdExtracted,&set);
                    pthread_mutex_unlock(&lock_set);
                pthread_mutex_unlock(&lock_lista_ON);  
            }
            
        }
        else{ //letto<=0
            //disconnetto anche in questo caso
            pthread_mutex_lock(&lock_lista_ON);
            HashElem* toDis=listaONFind(fdExtracted,lista_ON);
            int toDisIndex=0;
            if(toDis!=NULL) toDisIndex=hashing(toDis->nickname)%32;
            pthread_mutex_lock(&lock_userHashTable[toDisIndex]);

             

            if(toDis!=NULL && toDis->isOn==1){
                toDis->isOn=0;
                toDis->channel=-1;
                lista_ON=removeGroupMem(lista_ON,toDis->nickname);
                nusersON--;

                //aggiornamento stat
                pthread_mutex_lock(&lock_stat);
                chattyStats.nonline--;
                pthread_mutex_unlock(&lock_stat);
                //------------------------------

               
            }
            pthread_mutex_unlock(&lock_userHashTable[toDisIndex]);
            pthread_mutex_unlock(&lock_lista_ON);
            close(fdExtracted); printf("-----------------------------------------CHIUDO!!\n");
        }
        // prima di finire un ciclo pulisco il messaggio letto
        if(messExtracted->data.buf!=NULL){
            free(messExtracted->data.buf);
            messExtracted->data.buf=NULL;
        }
        free(messExtracted);
        messExtracted=NULL;
    }
    //pulisco l'ultimo messaggio letto prima di uscire
    if(messExtracted->data.buf!=NULL){
        free(messExtracted->data.buf);
        messExtracted->data.buf=NULL;
    }
    free(messExtracted);
    messExtracted=NULL;
    return 0;
}


//funzione da eseguire in caso di segnale di chiusurA
static void SigHand(){
    clean_bit=1;
}

//PULIZIA
static void PULIZIA(){
    close(sock);
    unlink(UnixPath);
    int j = 0;
    //-----Pulizia hashtable------
    HashElem *cleaner = NULL;
    HashElem *cleanerAux = NULL;
    for (j = 0; j < 1024; j++){
        if (userHashTable[j] != NULL){
            cleaner = userHashTable[j];
            while (cleaner != NULL){
                free(cleaner->nickname);
                messRec *cronoCleaner = cleaner->ricevuti;
                if (cleaner->toR > 0){
                    while (cronoCleaner != NULL){
                        free(cronoCleaner->received->data.buf);
                        free(cronoCleaner->received);
                        cronoCleaner = removeMess(cronoCleaner);
                        cleaner->toR--;
                    }
                }
                if (cleaner->isIn != NULL){
                    stringList *isInCleaner = cleaner->isIn;
                    stringList *isInCleaneraux = NULL;
                    while (isInCleaner != NULL){
                        isInCleaneraux = isInCleaner;
                        isInCleaner = isInCleaneraux->next;
                        free(isInCleaneraux->elem);
                        free(isInCleaneraux);
                    }
                }
                if (cleaner->isGroup == 1){
                    LinkEl *GroupCleaner = cleaner->GroupList;
                    while (GroupCleaner != NULL){
                        GroupCleaner = removeGroupMem(GroupCleaner, GroupCleaner->memberName);
                    }
                    free(cleaner->admin);
                }
                cleanerAux = cleaner;
                cleaner = cleaner->next;
                free(cleanerAux);
            }
        }
    }
    //--Pulisco la lista degli utenti connessi
    LinkEl *pulitore = lista_ON;
    LinkEl *pulitoreaux = NULL;
    while (pulitore != NULL){
        pulitoreaux = pulitore;
        free(pulitore->memberName);
        pulitore = pulitoreaux->next;
        free(pulitoreaux);
    }
    //Pulizia
    write(1, "\nRicevuto SIGINT\n", 17);
    if (lista_ON != NULL)
        free(lista_ON);
    free(fd_queue->Coda);
    free(fd_queue);
    free(userHashTable);
    free(UnixPath);
    free(DirName);
    free(StatFileName);
    if (messletto != NULL)
        free(messletto);
    return;
}



//Statistiche

static void Stat_Hand(){
    stat_bit=1;
}

static void STATISTICHE(){
    FILE* fstat;
    errno=0;
    fstat=fopen(StatFileName,"wb");
    if(errno!=0){
        perror("Apertura file di statistiche");
        errno=0;
        return;
    }
    //scrivo le statistiche nel file delle statitische
    printStats(fstat);
    fclose(fstat); // e chiudo il file... delle statistiche.
    return;
}



//MAIN
int main(int argc, char* argv[]){
    //creazione tabella hash
    int threadActive=0;
    int i=0;
    for(i=0;i<32;i++){
        pthread_mutex_init(&lock_userHashTable[i],NULL);
    }
    userHashTable=malloc(1024*sizeof(HashElem*));
    for(i=0;i<1024;i++){
        userHashTable[i]=NULL;
    }
    //controllo argomenti
    if((argc!=3)||(strcmp(argv[1],"-f")!=0)){
        usage(argv[0]);
        return -1;
    }
    //PARSER PER IL FILE .conf
    FILE* fconf;
    errno=0;
    fconf=fopen(argv[2],"r");
    if(errno!=0){
        perror("Apertura file");
        return -1; }
    char* row=(char*)malloc(100*sizeof(char)); //alloco una stringa di 100 caratteri che dovrà contenere una linea del file di configurazione
    memset(row,0,100*sizeof(char));
    while(fgets(row,100,fconf)){ //prendo una linea
        if(strncmp(row,"#",1)!=0){ // se la prima lettera è diversa da # allora potrebbe essere una "linea buona"
            
            //Ricerca delle parole chiave 

            //se trovo corrispondenza estraggo il dato dal file
            if(strncmp("UnixPath",row,8)==0) {
                UnixPath=calloc(100,sizeof(char)); 
                char* extracted=exdato(row);
                strcpy(UnixPath,extracted);
                free(extracted);
                }
            if(strncmp("MaxConnections",row,14)==0) { MaxConnections = exdatoi(row);}
            if(strncmp("ThreadsInPool",row,13)==0) { ThreadsInPool = exdatoi(row);}
            if(strncmp("MaxMsgSize",row,10)==0) { MaxMsgSize = exdatoi(row);}
            if(strncmp("MaxFileSize",row,11)==0) { MaxFileSize = exdatoi(row);}
            if(strncmp("MaxHistMsgs",row,11)==0) { MaxHistMsgs = exdatoi(row);}
            if(strncmp("DirName",row,7)==0) {
                DirName=calloc(100,sizeof(char)); 
                char* extracted=exdato(row);
                int dimEx=strlen(extracted);
                strncpy(DirName,extracted,dimEx-1);
                free(extracted);
                }
            if(strncmp("StatFileName",row,10)==0) {
                StatFileName=malloc(100*sizeof(char)); 
                char* extracted=exdato(row);
                strcpy(StatFileName,extracted);
                free(extracted);
                }
        }
    }
    errno=0;
    //chiudo il file di configurazione
    fclose(fconf);
    if(errno!=0){ perror("Chiusura File"); FALLIMENTO }
    free(row);

    

    //gestione segnali
    struct sigaction s;
    memset( &s, 0, sizeof(s) );
    s.sa_handler=SigHand; 
    //se mi arriva uno di questi segnali faccio partire la pulizia e chiusura del server
    sigaction(SIGINT,&s,NULL);
    sigaction(SIGQUIT,&s,NULL); 
    sigaction(SIGTERM,&s,NULL); 

    //se mi arriva SIGUSR1 scrivo le statistiche
    struct sigaction v;
    memset( &v, 0, sizeof(v) );
    v.sa_handler=Stat_Hand;
     



    //usr1
    sigaction(SIGUSR1,&v,NULL);
    //ignoro SIGPIPE
    signal(SIGPIPE,SIG_IGN);
    
     

    //creazione socket principale
    int success=0;
    int canale;
    int auxbool=0;
    struct sockaddr_un isocket;
    unlink(UnixPath);
    if(errno!=0){
        perror("unlink");
        errno=0;
    }
    while(!success){
        success=1;
        strncpy(isocket.sun_path,UnixPath,UNIX_PATH_MAX);
        isocket.sun_family=AF_UNIX;
        sock=socket(AF_UNIX,SOCK_STREAM,0);
        if(errno!=0 || auxbool){
            perror("socket");
            success=0; 
            errno=0;
            close(sock);
        }
    }
        

    bind(sock,(struct sockaddr*)&isocket,sizeof(isocket));
    if(errno!=0){perror("bind");success=0; errno=0;}
    listen(sock,MaxConnections);
    if(errno!=0){perror("listen");success=0;errno=0;}
    printf("SOCK: %d\n",sock);
    printf("CONNESSIONE RIUSCITA!\n");
    


    //SELECT
    fd_queue=CreateQueue(MaxConnections,fd_queue); 
   
    int fd;
    if(sock>fdnum) fdnum=sock;
    //setto a zero il set
    FD_ZERO(&set);
    //aggiungo il fd principale al set
    FD_SET(sock,&set);


    
    //timer per la select
    struct timeval timeout;
    timeout.tv_sec=0;
    timeout.tv_usec=3000;

    clean_bit=0; //imposto il bit di pulizia a 0
    pthread_t tid[ThreadsInPool]; //array di ThreadID
    while(!clean_bit){
        if(stat_bit){
            STATISTICHE();
            stat_bit=0;
        }
        pthread_mutex_lock(&lock_set);
        rdset=set; //ripristino rdset
        pthread_mutex_unlock(&lock_set);

        if(select(fdnum+1,&rdset,NULL,NULL,&timeout)<0){
            if(errno>0 && errno!=4) { perror("SELECT FALLITA"); errno=0;}
        }
        else{
            timeout.tv_sec=0;
            timeout.tv_usec=3000;
            for(fd=0;fd<=fdnum;fd++){
                if(FD_ISSET(fd,&rdset)){
                    printf("fd:%d è pronto\n",fd);
                    if(fd==sock){
                        canale=accept(sock,NULL,0);
                        pthread_mutex_lock(&lock_set);
                        FD_SET(canale,&set);
                        pthread_mutex_unlock(&lock_set);
                        printf("FACCIO LA ACCEPT su canale: %d e fd:%d\n",canale,fd);
                        if(canale>fdnum) fdnum=canale;
                    }
                    else {
                        //tolgo il fd dal set
                        pthread_mutex_lock(&lock_set);
                        FD_CLR(fd,&set);
                        pthread_mutex_unlock(&lock_set);

                        //devo mettere il fd in una coda
                        int added=-1;
                        pthread_mutex_lock(&lock_fd_queue);
                        added=PushQueue(fd,fd_queue);
                        printf("added=%d\n",added);
                        while(added==-1){
                            //se coda piena
                            pthread_cond_wait(&cond_fd_queue_Push,&lock_fd_queue);
                            added=PushQueue(fd,fd_queue);
                            if(clean_bit){
                                pthread_mutex_unlock(&lock_fd_queue);
                                pthread_mutex_unlock(&lock_set);
                                return 0;
                            }
                        }
                        pthread_cond_signal(&cond_fd_queue_Pull);
                        pthread_mutex_unlock(&lock_fd_queue);
                        
                    }
                }
            }
        }
        //------------------------
        
        //MULTITHREADING
        pthread_mutex_lock(&lock_threadActive);
        
        if(threadActive<ThreadsInPool){
            
            threadActive++;
            printf("thread ATTIVI:%d\n",threadActive);
            if(pthread_create(&tid[threadActive-1],NULL,&threadOp,NULL)!=0){
                perror("CREAZIONE THREAD FALLITA");
                errno=0;
            }
        }
       
       pthread_mutex_unlock(&lock_threadActive);
    }
    //terminazione thread
    int d=0;
    pthread_cond_broadcast(&cond_fd_queue_Pull);
    pthread_cond_broadcast(&cond_fd_queue_Push);
    for(d=0;d<ThreadsInPool;d++){
        printf("Aspetto il thread numero %d\n",d);
        pthread_join(tid[d],NULL);
    }
    //pulisco tutto e termino
    PULIZIA();
 
    return 0;
}