#
# chatterbox Progetto del corso di LSO 2017 
#
# Dipartimento di Informatica Università di Pisa
# Docenti: Prencipe, Torquati
# Studente: Fabio Catinella
# Matricola: 517665
# Corso: B
#



#!/bin/bash

#controllo se sono stati passati due argomenti
if (( $# < 2 )); then
    echo "Uso: $0  file.conf tempo"
    exit 1
fi

#controllo se -help è stato passato come argomento
for i in "$@"
do
    if [ $i == -help ]; then
        echo "Help richiesto -> Uso: $0 file.conf tempo"
        exit 1
    fi
done

#controllo che il file passato esista
if ! [ -s $1 ]; then
    echo "File specificato NON esistente o vuoto"
    exit 1
fi

#prendo 
DirName=$( cat $1 ) 

#Uso Pattern Matching
#tolgo tutto quello che c'è prima di "DirName"
DirName=${DirName##*"DirName"}
#tolgo tutto quello che c'è dopo '#'
DirName=${DirName%%'#'*}
# sostituisco tutti gli spazi con nulla
DirName=${DirName//[[:space:]]/}
#tolgo lo spazio e l'uguale
DirName=/${DirName#*'/'}


#se t==0 stampo la lista dei file contenuti in DirName
if (( $2 == 0 )); then
    echo "Elementi in $DirName:"
    ls $DirName
    exit 1
fi

#lista file contenuti nella cartella
Lista=$DirName
Lista=$Lista/*

#se t>0 rimuovo tutti i file più vecchi di t minuti
if (( $2 > 0 )); then
    #prendo il numero dei secondi attuali dopo 1 Gen 1970
    Minuti=$(date +%s)
    #mi trovo i minuti
    Minuti=$( expr $Minuti / 60 )
    for e in $Lista
    do  
        #se il file esiste
        if [ -f $e ]; then
            #prendo il numero dei secondi dopo 1 Gen 1970 
            MinutiFile=$(stat -c %Y $e)
            #mi trovo i minuti
            MinutiFile=$( expr $MinutiFile / 60)
            #faccio la differenza
            Diff=$( expr $Minuti - $MinutiFile )
            if ((Diff > $2 )); then
                #elimino nel caso sennò proseguo
                echo "Elimino $e"
                rm $e
            fi
        fi
    done
fi

echo "Fine script"