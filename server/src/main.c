#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>      // Gestione dei segnali del Sistema Operativo
#include <pthread.h>     // Thread POSIX per la concorrenza
#include <sys/socket.h>  // Funzioni fondamentali dei socket (socket, bind, listen, accept)
#include <netinet/in.h>  // Strutture per indirizzi Internet IPv4 (struct sockaddr_in)
#include <arpa/inet.h>   // Conversioni di formato (htons, inet_ntop)

#include "protocol.h"
#include "session.h"
#include "client_handler.h"

static int server_socket = -1;


// Gestore di segnali per la terminazione controllata del server
void handle_signal(int sig) {
    printf("\n[SERVER] Segnale %d ricevuto. Arresto...\n", sig);
    if (server_socket != -1) { close(server_socket); }
    exit(0);
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    if (argc >= 2) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Uso: %s [porta]\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    /* Ignora SIGPIPE per evitare terminazioni anomale se un client chiude il socket */
    signal(SIGPIPE, SIG_IGN);
    //per segnali di interruzioni da tastiera e terminazione del server
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    sessions_init();

    server_socket = socket(AF_INET, SOCK_STREAM, 0); //creiamo una socket che usa ipv4 , con trasmissione affidabile e ordinata di byte (tcp)
    if (server_socket < 0) { // se il valore restituito è minore di 0, significa che c'è stato un errore
        perror("[ERRORE] Creazione socket server fallita");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    //modifica opzioni socket , riutilizzo indirizzo e porta in modo da poter riavviare il server subito dopo averlo chiuso
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("[ATTENZIONE] setsockopt SO_REUSEADDR fallita");
    }

    struct sockaddr_in server_addr; //struttura per l'indirizzo del server IPV4
    memset(&server_addr, 0, sizeof(server_addr)); //usiamo memset per evitare che rimangano residui di dati vecchi
    server_addr.sin_family = AF_INET; //famiglia di indirizzi ipv4
    server_addr.sin_addr.s_addr = INADDR_ANY; //qualsiasi interfaccia di rete
    server_addr.sin_port = htons(port); //porta del server in formato host to network short

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("[ERRORE] Bind fallita");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, 10) < 0) {
        perror("[ERRORE] Listen fallita");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    printf("=========================================\n");
    printf("   Server Forza 4 avviato sulla porta %d  \n", port);
    printf("=========================================\n");

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr); //tipo intero unsigned
        int client_fd = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);

        if (client_fd < 0) {
            perror("[ATTENZIONE] Accept fallita");
            continue;
        }

        //conversione indirizzo ip in formato leggibile
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        printf("[CONNESSIONE] Nuovo client da %s:%d (fd: %d)\n",client_ip, ntohs(client_addr.sin_port), client_fd);

        //allocazione memoria e assegnazione puntatore all'indirizzo del socket (previene race condition)
        int *pclient = malloc(sizeof(int));
        if (!pclient) {
            close(client_fd);
            continue;
        }
        *pclient = client_fd;

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_thread, pclient) != 0) {
            perror("[ERRORE] Creazione thread fallita");
            close(client_fd);
            free(pclient);
        } else {
            pthread_detach(tid); // il main non eseguirà mai la join
        }
    }

    close(server_socket);
    return 0;
}

