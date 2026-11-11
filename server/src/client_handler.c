#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/socket.h>

#include "client_handler.h"
#include "session.h"
#include "game.h"
#include "protocol.h"

static void send_msg(int fd, const char *msg) {
    if (fd <= 0) return;
    send(fd, msg, strlen(msg), 0);
}

static void trim_crlf(char *str) {
    int len = strlen(str);
    while (len > 0 && (str[len - 1] == '\r' || str[len - 1] == '\n' || str[len - 1] == ' ')) {
        str[len - 1] = '\0';
        len--;
    }
}

/* Gestione autenticazione iniziale */
static int handle_auth(int client_fd, const char *line, int *authenticated, char *username) {
    if (strncmp(line, "NICK ", 5) != 0) {
        send_msg(client_fd, "ERR Autenticazione richiesta con: NICK <nome>\n");
        return 0;
    }

    const char *nick = line + 5;
    if (strlen(nick) == 0 || strlen(nick) >= MAX_NAME_LEN) {
        send_msg(client_fd, "ERR Nickname non valido\n");
        return 0;
    }

    int res = client_register(client_fd, nick);
    if (res == 0) {
        strncpy(username, nick, MAX_NAME_LEN - 1);
        username[MAX_NAME_LEN - 1] = '\0';
        *authenticated = 1;

        printf("[LOGIN] Client (fd: %d) registrato come '%s'\n", client_fd, username);
        fflush(stdout);

        char welcome[BUFFER_SIZE];
        snprintf(welcome, sizeof(welcome), "OK Benvenuto %s\n", username);
        send_msg(client_fd, welcome);
        send_lobby_state(client_fd);
        return 0;
    }

    if (res == -1) {
        send_msg(client_fd, "ERR Nickname gia in uso\n");
        return 0;
    }

    send_msg(client_fd, "ERR Server pieno\n");
    return -1;
}

static void handle_create(int client_fd) {
    int rid = room_create(client_fd);
    if (rid > 0) {
        printf("[STANZA] Creata nuova stanza #%d (owner fd: %d)\n", rid, client_fd);
        fflush(stdout);
        char resp[BUFFER_SIZE];
        snprintf(resp, sizeof(resp), "ROOM_CREATED %d\n", rid);
        send_msg(client_fd, resp);
    } else if (rid == -2) {
        send_msg(client_fd, "ERR Stai gia giocando attivamente in un'altra partita\n");
    } else {
        send_msg(client_fd, "ERR Impossibile creare la stanza\n");
    }
}

static void handle_join(int client_fd, const char *args) {
    int target_id = atoi(args);
    int res = room_request_join(target_id, client_fd);

    if (res == 0) {
        printf("[SFIDA] Client (fd: %d) ha richiesto di entrare nella stanza #%d\n", client_fd, target_id);
        fflush(stdout);
        send_msg(client_fd, "OK Richiesta inviata. Attendi la decisione del proprietario.\n");
    } else if (res == -1) {
        send_msg(client_fd, "ERR Stai gia giocando attivamente in un'altra partita\n");
    } else if (res == -2) {
        send_msg(client_fd, "ERR Stanza inesistente\n");
    } else if (res == -3) {
        send_msg(client_fd, "ERR Stanza non disponibile per l'accesso\n");
    } else if (res == -4) {
        send_msg(client_fd, "ERR Non puoi unirti alla tua stessa stanza\n");
    } else if (res == -5) {
        send_msg(client_fd, "ERR Un altro giocatore ha gia una richiesta in attesa\n");
    } else {
        send_msg(client_fd, "ERR Errore durante la richiesta di join\n");
    }
}

static void handle_accept(int client_fd, const char *args) {
    int rid = atoi(args);
    if (room_accept_join(rid, client_fd) != 0) {
        send_msg(client_fd, "ERR Impossibile accettare la richiesta\n");
    } else {
        printf("[PARTITA] Stanza #%d: Richiesta accettata, partita avviata!\n", rid);
        fflush(stdout);
    }
}

static void handle_reject(int client_fd, const char *args) {
    int rid = atoi(args);
    if (room_reject_join(rid, client_fd) != 0) {
        send_msg(client_fd, "ERR Impossibile rifiutare la richiesta\n");
    }
}

static void handle_move(int client_fd, const char *args) {
    int rid = -1;
    int col = -1;
    if (sscanf(args, "%d %d", &rid, &col) != 2) {
        send_msg(client_fd, "ERR Formato mossa non valido. Uso: MOVE <id> <colonna>\n");
        return;
    }

    col -= 1; /* Conversione da indice 1-7 a 0-6 */
    int row = -1, game_over = 0, winner_fd = -1;
    char board_str[BOARD_ROWS * BOARD_COLS + 1] = {0};
    int owner_fd = -1, guest_fd = -1, next_turn_fd = -1;

    int res = room_make_move(rid, client_fd, col, &row, &game_over, &winner_fd,
                             board_str, &owner_fd, &guest_fd, &next_turn_fd);

    if (res == -2) {
        send_msg(client_fd, "ERR Non e il tuo turno\n");
        return;
    }
    if (res == -3) {
        send_msg(client_fd, "ERR Mossa non valida o colonna piena\n");
        return;
    }
    if (res != 0) {
        send_msg(client_fd, "ERR Errore durante l'esecuzione della mossa\n");
        return;
    }

    char sym = (client_fd == owner_fd) ? CELL_P1 : CELL_P2;
    char board_msg[BUFFER_SIZE];
    snprintf(board_msg, sizeof(board_msg), "BOARD_UPDATE %d %d %c %s\n",
             col + 1, row, sym, board_str);

    printf("[MOSSA] Stanza #%d: client fd %d (%c) inserisce in colonna %d (riga %d)\n",
           rid, client_fd, sym, col + 1, row);
    fflush(stdout);

    send_msg(owner_fd, board_msg);
    send_msg(guest_fd, board_msg);

    if (game_over == 1) {
        /* Vittoria */
        int loser_fd = (winner_fd == owner_fd) ? guest_fd : owner_fd;
        printf("[FINE GIOCO] Stanza #%d: Vittoria del client fd %d! (Perdente: fd %d)\n", rid, winner_fd, loser_fd);
        fflush(stdout);
        send_msg(winner_fd, "GAME_OVER WIN Complimenti, hai vinto!\n");
        send_msg(loser_fd, "GAME_OVER LOSE Hai perso. Sarai reindirizzato alla lobby.\n");

        char fin_notif[BUFFER_SIZE];
        snprintf(fin_notif, sizeof(fin_notif), "ROOM_NOTIF %d TERMINATA Conclusa\n", rid);
        broadcast_lobby(fin_notif, -1);

        send_msg(winner_fd, "REMATCH_OFFER Vuoi creare una nuova sessione da proprietario? (REMATCH YES / NO)\n");
    } else if (game_over == 2) {
        /* Pareggio */
        printf("[FINE GIOCO] Stanza #%d: Partita conclusa in PAREGGIO.\n", rid);
        fflush(stdout);
        send_msg(owner_fd, "GAME_OVER DRAW Partita terminata in parita.\n");
        send_msg(guest_fd, "GAME_OVER DRAW Partita terminata in parita.\n");

        char fin_notif[BUFFER_SIZE];
        snprintf(fin_notif, sizeof(fin_notif), "ROOM_NOTIF %d TERMINATA Pareggio\n", rid);
        broadcast_lobby(fin_notif, -1);

        send_msg(owner_fd, "REMATCH_OFFER Desideri concordare una nuova partita? (REMATCH YES / NO)\n");
        send_msg(guest_fd, "REMATCH_OFFER Desideri concordare una nuova partita? (REMATCH YES / NO)\n");
    } else {
        /* Prossimo turno */
        send_msg(next_turn_fd, "YOUR_TURN\n");
        int waiting_fd = (next_turn_fd == owner_fd) ? guest_fd : owner_fd;
        send_msg(waiting_fd, "WAIT_TURN\n");
    }
}

static void handle_rematch(int client_fd, const char *args) {
    int rid = -1;
    char choice[8] = {0};
    if (sscanf(args, "%d %s", &rid, choice) != 2) return;

    int vote_yes = (strcasecmp(choice, "YES") == 0);
    int new_session_ready = 0;
    int owner_fd = -1;
    int res = room_handle_rematch(rid, client_fd, vote_yes, &new_session_ready, &owner_fd);

    if (res == 1) {
        send_msg(client_fd, "REMATCH_OK Sei il proprietario della nuova sessione in attesa di sfidanti.\n");
    } else if (res == 2) {
        if (owner_fd != -1) {
            send_msg(owner_fd, "REMATCH_OK Accordo raggiunto. Nuova sessione in attesa aperta.\n");
        }
    } else if (!vote_yes) {
        send_msg(client_fd, "OK Sessione conclusa.\n");
        client_set_active_room(client_fd, -1);
    }
}

static void handle_leave(int client_fd, const char *args) {
    int rid = atoi(args);
    room_close(rid);
    send_msg(client_fd, "OK Sei uscito dalla stanza.\n");
}

/* Dispatcher dei comandi autenticati */
static int dispatch_command(int client_fd, const char *line) {
    if (strcmp(line, "CREATE") == 0) {
        handle_create(client_fd);
    } else if (strcmp(line, "LIST") == 0) {
        send_lobby_state(client_fd);
    } else if (strncmp(line, "JOIN ", 5) == 0) {
        handle_join(client_fd, line + 5);
    } else if (strncmp(line, "ACCEPT ", 7) == 0) {
        handle_accept(client_fd, line + 7);
    } else if (strncmp(line, "REJECT ", 7) == 0) {
        handle_reject(client_fd, line + 7);
    } else if (strncmp(line, "MOVE ", 5) == 0) {
        handle_move(client_fd, line + 5);
    } else if (strncmp(line, "REMATCH ", 8) == 0) {
        handle_rematch(client_fd, line + 8);
    } else if (strncmp(line, "LEAVE ", 6) == 0) {
        handle_leave(client_fd, line + 6);
    } else if (strcmp(line, "QUIT") == 0) {
        send_msg(client_fd, "BYE\n");
        return -1; /* Richiesta di disconnessione */
    } else {
        send_msg(client_fd, "ERR Comando non riconosciuto\n");
    }
    return 0;
}

void* client_thread(void *arg) {
    int client_fd = *((int*)arg);
    free(arg);

    char recv_buf[BUFFER_SIZE];
    char line_buf[BUFFER_SIZE];
    int line_len = 0;
    int authenticated = 0;
    char username[MAX_NAME_LEN] = {0};

    while (1) {
        ssize_t bytes_read = recv(client_fd, recv_buf, sizeof(recv_buf) - 1, 0);
        if (bytes_read <= 0) {
            break;
        }
        recv_buf[bytes_read] = '\0';

        for (ssize_t i = 0; i < bytes_read; i++) {
            char c = recv_buf[i];
            if (c == '\n') {
                line_buf[line_len] = '\0';
                trim_crlf(line_buf);

                if (line_len > 0) {
                    if (!authenticated) {
                        if (handle_auth(client_fd, line_buf, &authenticated, username) < 0) {
                            close(client_fd);
                            return NULL;
                        }
                    } else {
                        if (dispatch_command(client_fd, line_buf) < 0) {
                            goto disconnect;
                        }
                    }
                }
                line_len = 0;
            } else {
                if (line_len < (int)sizeof(line_buf) - 1) {
                    line_buf[line_len++] = c;
                }
            }
        }
    }

disconnect:
    if (authenticated) {
        printf("[DISCONNESSIONE] Client '%s' (fd: %d) disconnesso.\n", username, client_fd);
        fflush(stdout);
        room_handle_disconnect(client_fd);
        client_unregister(client_fd);
    }
    close(client_fd);
    return NULL;
}
