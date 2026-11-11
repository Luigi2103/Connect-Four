#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include "session.h"
#include "game.h"

static ConnectedClient clients[MAX_CLIENTS];
static GameRoom rooms[MAX_ROOMS];
static pthread_mutex_t session_lock;
static int next_room_id = 1;

void sessions_init(void) {
    pthread_mutex_init(&session_lock, NULL);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].socket_fd = -1;
        clients[i].username[0] = '\0';
        clients[i].active_room_id = -1;
    }

    for (int i = 0; i < MAX_ROOMS; i++) {
        rooms[i].id = -1;
        rooms[i].state = STATE_CREATED;
        rooms[i].owner_fd = -1;
        rooms[i].guest_fd = -1;
        rooms[i].pending_fd = -1;
        rooms[i].owner_name[0] = '\0';
        rooms[i].guest_name[0] = '\0';
        rooms[i].pending_name[0] = '\0';
        rooms[i].winner_fd = -1;
        rooms[i].rematch_owner_vote = 0;
        rooms[i].rematch_guest_vote = 0;
    }
}

int client_register(int socket_fd, const char *username) {
    pthread_mutex_lock(&session_lock);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket_fd != -1 && strcmp(clients[i].username, username) == 0) {
            pthread_mutex_unlock(&session_lock);
            return -1; /* Username già in uso */
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket_fd == -1) {
            clients[i].socket_fd = socket_fd;
            strncpy(clients[i].username, username, MAX_NAME_LEN - 1);
            clients[i].username[MAX_NAME_LEN - 1] = '\0';
            clients[i].active_room_id = -1;
            pthread_mutex_unlock(&session_lock);
            return 0;
        }
    }

    pthread_mutex_unlock(&session_lock);
    return -2; /* Server pieno */
}

void client_unregister(int socket_fd) {
    pthread_mutex_lock(&session_lock);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket_fd == socket_fd) {
            clients[i].socket_fd = -1;
            clients[i].username[0] = '\0';
            clients[i].active_room_id = -1;
            break;
        }
    }

    pthread_mutex_unlock(&session_lock);
}

ConnectedClient* client_find_by_fd(int socket_fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket_fd == socket_fd) {
            return &clients[i];
        }
    }
    return NULL;
}

int client_is_actively_playing(int socket_fd) {
    pthread_mutex_lock(&session_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket_fd == socket_fd) {
            int active = (clients[i].active_room_id != -1);
            pthread_mutex_unlock(&session_lock);
            return active;
        }
    }
    pthread_mutex_unlock(&session_lock);
    return 0;
}

void client_set_active_room(int socket_fd, int room_id) {
    pthread_mutex_lock(&session_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket_fd == socket_fd) {
            clients[i].active_room_id = room_id;
            break;
        }
    }
    pthread_mutex_unlock(&session_lock);
}

static void send_raw(int fd, const char *msg) {
    if (fd <= 0) return;
    send(fd, msg, strlen(msg), 0);
}

void broadcast_lobby(const char *message, int exclude_fd) {
    pthread_mutex_lock(&session_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket_fd != -1 && clients[i].socket_fd != exclude_fd) {
            /* Notifica inviata solo ai client che non sono occupati in gioco attivo */
            if (clients[i].active_room_id == -1) {
                send_raw(clients[i].socket_fd, message);
            }
        }
    }
    pthread_mutex_unlock(&session_lock);
}

void send_lobby_state(int target_fd) {
    pthread_mutex_lock(&session_lock);
    char buffer[BUFFER_SIZE];

    for (int i = 0; i < MAX_ROOMS; i++) {
        if (rooms[i].id != -1) {
            const char *state_str = "SCONOSCIUTO";
            if (rooms[i].state == STATE_WAITING) state_str = "IN_ATTESA";
            else if (rooms[i].state == STATE_IN_PROGRESS) state_str = "IN_CORSO";
            else if (rooms[i].state == STATE_FINISHED) state_str = "TERMINATA";

            snprintf(buffer, sizeof(buffer), "LOBBY_ROOM %d %s %s\n",
                     rooms[i].id, state_str, rooms[i].owner_name);
            send_raw(target_fd, buffer);
        }
    }
    pthread_mutex_unlock(&session_lock);
}

int room_create(int owner_fd) {
    pthread_mutex_lock(&session_lock);

    /* Verifica che il client non sia già impegnato in una partita attiva */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket_fd == owner_fd && clients[i].active_room_id != -1) {
            pthread_mutex_unlock(&session_lock);
            return -2;
        }
    }

    int free_slot = -1;
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (rooms[i].id == -1) {
            free_slot = i;
            break;
        }
    }

    if (free_slot == -1) {
        pthread_mutex_unlock(&session_lock);
        return -1; /* Limite stanze raggiunto */
    }

    ConnectedClient *cli = client_find_by_fd(owner_fd);
    if (!cli) {
        pthread_mutex_unlock(&session_lock);
        return -3;
    }

    GameRoom *r = &rooms[free_slot];
    r->id = next_room_id++;
    r->state = STATE_WAITING;
    r->owner_fd = owner_fd;
    strncpy(r->owner_name, cli->username, MAX_NAME_LEN - 1);
    r->owner_name[MAX_NAME_LEN - 1] = '\0';
    r->guest_fd = -1;
    r->guest_name[0] = '\0';
    r->pending_fd = -1;
    r->pending_name[0] = '\0';
    r->winner_fd = -1;
    r->rematch_owner_vote = 0;
    r->rematch_guest_vote = 0;
    game_init(r->board);

    int created_id = r->id;
    char owner_nick[MAX_NAME_LEN];
    strncpy(owner_nick, r->owner_name, MAX_NAME_LEN);

    pthread_mutex_unlock(&session_lock);

    /* Notifica a tutti gli altri client in lobby */
    char notif[BUFFER_SIZE];
    snprintf(notif, sizeof(notif), "ROOM_NOTIF %d IN_ATTESA %s\n", created_id, owner_nick);
    broadcast_lobby(notif, owner_fd);

    return created_id;
}

GameRoom* room_find_by_id(int room_id) {
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (rooms[i].id == room_id) {
            return &rooms[i];
        }
    }
    return NULL;
}

int room_request_join(int room_id, int guest_fd) {
    pthread_mutex_lock(&session_lock);

    /* Verifica se il guest sta già giocando */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket_fd == guest_fd && clients[i].active_room_id != -1) {
            pthread_mutex_unlock(&session_lock);
            return -1; /* Già impegnato in partita attiva */
        }
    }

    GameRoom *r = room_find_by_id(room_id);
    if (!r) {
        pthread_mutex_unlock(&session_lock);
        return -2; /* Stanza non trovata */
    }

    if (r->state != STATE_WAITING) {
        pthread_mutex_unlock(&session_lock);
        return -3; /* Non in attesa */
    }

    if (r->owner_fd == guest_fd) {
        pthread_mutex_unlock(&session_lock);
        return -4; /* Non puoi unirti alla tua stessa stanza */
    }

    if (r->pending_fd != -1) {
        pthread_mutex_unlock(&session_lock);
        return -5; /* C'è già una richiesta pendente in valutazione */
    }

    ConnectedClient *cli = client_find_by_fd(guest_fd);
    if (!cli) {
        pthread_mutex_unlock(&session_lock);
        return -6;
    }

    r->pending_fd = guest_fd;
    strncpy(r->pending_name, cli->username, MAX_NAME_LEN - 1);
    r->pending_name[MAX_NAME_LEN - 1] = '\0';

    int owner_fd = r->owner_fd;
    char guest_nick[MAX_NAME_LEN];
    strncpy(guest_nick, cli->username, MAX_NAME_LEN);

    pthread_mutex_unlock(&session_lock);

    /* Invia la richiesta al creatore della stanza */
    char req_msg[BUFFER_SIZE];
    snprintf(req_msg, sizeof(req_msg), "JOIN_REQUEST %d %s\n", room_id, guest_nick);
    send_raw(owner_fd, req_msg);

    return 0;
}

int room_accept_join(int room_id, int owner_fd) {
    pthread_mutex_lock(&session_lock);

    GameRoom *r = room_find_by_id(room_id);
    if (!r || r->owner_fd != owner_fd) {
        pthread_mutex_unlock(&session_lock);
        return -1;
    }

    if (r->pending_fd == -1) {
        pthread_mutex_unlock(&session_lock);
        return -2; /* Nessuna richiesta pendente */
    }

    r->guest_fd = r->pending_fd;
    strncpy(r->guest_name, r->pending_name, MAX_NAME_LEN);
    r->pending_fd = -1;
    r->pending_name[0] = '\0';

    /* Avvio della partita */
    r->state = STATE_IN_PROGRESS;
    r->current_turn_fd = r->owner_fd; /* Il creatore inizia per primo */
    game_init(r->board);

    /* Aggiorna stato attivo dei client */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket_fd == r->owner_fd || clients[i].socket_fd == r->guest_fd) {
            clients[i].active_room_id = room_id;
        }
    }

    int guest_fd = r->guest_fd;
    char owner_name[MAX_NAME_LEN];
    char guest_name[MAX_NAME_LEN];
    strncpy(owner_name, r->owner_name, MAX_NAME_LEN);
    strncpy(guest_name, r->guest_name, MAX_NAME_LEN);

    pthread_mutex_unlock(&session_lock);

    /* Messaggi di inizio partita ai due giocatori */
    char start_owner[BUFFER_SIZE];
    char start_guest[BUFFER_SIZE];
    snprintf(start_owner, sizeof(start_owner), "GAME_START %d %s X 1\n", room_id, guest_name);
    snprintf(start_guest, sizeof(start_guest), "GAME_START %d %s O 0\n", room_id, owner_name);
    send_raw(owner_fd, start_owner);
    send_raw(guest_fd, start_guest);

    /* Notifica gli altri client che la partita è in corso */
    char notif[BUFFER_SIZE];
    snprintf(notif, sizeof(notif), "ROOM_NOTIF %d IN_CORSO %s\n", room_id, owner_name);
    broadcast_lobby(notif, -1);

    return 0;
}

int room_reject_join(int room_id, int owner_fd) {
    pthread_mutex_lock(&session_lock);

    GameRoom *r = room_find_by_id(room_id);
    if (!r || r->owner_fd != owner_fd) {
        pthread_mutex_unlock(&session_lock);
        return -1;
    }

    if (r->pending_fd == -1) {
        pthread_mutex_unlock(&session_lock);
        return -2;
    }

    int rejected_fd = r->pending_fd;
    r->pending_fd = -1;
    r->pending_name[0] = '\0';

    pthread_mutex_unlock(&session_lock);

    char rej_msg[BUFFER_SIZE];
    snprintf(rej_msg, sizeof(rej_msg), "JOIN_REJECTED %d\n", room_id);
    send_raw(rejected_fd, rej_msg);

    return 0;
}

int room_make_move(int room_id, int player_fd, int col, int *out_row, int *out_game_over, 
                   int *out_winner_fd, char *out_board_str, int *out_owner_fd, int *out_guest_fd, int *out_next_turn_fd) {
    pthread_mutex_lock(&session_lock);

    GameRoom *r = room_find_by_id(room_id);
    if (!r || r->state != STATE_IN_PROGRESS) {
        pthread_mutex_unlock(&session_lock);
        return -1;
    }

    if (r->current_turn_fd != player_fd) {
        pthread_mutex_unlock(&session_lock);
        return -2; /* Non è il turno del giocatore */
    }

    char piece = (player_fd == r->owner_fd) ? CELL_P1 : CELL_P2;
    int row = game_drop_piece(r->board, col, piece);
    if (row == -1) {
        pthread_mutex_unlock(&session_lock);
        return -3; /* Mossa non valida (colonna piena o fuori range) */
    }

    *out_row = row;
    *out_owner_fd = r->owner_fd;
    *out_guest_fd = r->guest_fd;
    game_serialize(r->board, out_board_str);

    if (game_check_win(r->board, piece)) {
        r->state = STATE_FINISHED;
        r->winner_fd = player_fd;
        *out_game_over = 1;
        *out_winner_fd = player_fd;
        *out_next_turn_fd = -1;

        /* Il perdente deve lasciare subito la partita e tornare libero in lobby */
        int loser_fd = (player_fd == r->owner_fd) ? r->guest_fd : r->owner_fd;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].socket_fd == loser_fd) {
                clients[i].active_room_id = -1;
                break;
            }
        }
    } else if (game_is_full(r->board)) {
        r->state = STATE_FINISHED;
        r->winner_fd = -1;
        *out_game_over = 2; /* Pareggio */
        *out_winner_fd = -1;
        *out_next_turn_fd = -1;
    } else {
        /* Alterna il turno */
        r->current_turn_fd = (player_fd == r->owner_fd) ? r->guest_fd : r->owner_fd;
        *out_game_over = 0;
        *out_winner_fd = -1;
        *out_next_turn_fd = r->current_turn_fd;
    }

    pthread_mutex_unlock(&session_lock);
    return 0;
}

int room_handle_rematch(int room_id, int player_fd, int vote_yes, int *out_new_session_ready, int *out_owner_fd) {
    pthread_mutex_lock(&session_lock);

    *out_new_session_ready = 0;
    if (out_owner_fd) *out_owner_fd = -1;
    GameRoom *r = room_find_by_id(room_id);
    if (!r || r->state != STATE_FINISHED) {
        pthread_mutex_unlock(&session_lock);
        return -1;
    }

    /* Caso Vittoria: opzionale da traccia
       "il vincitore di una partita può decidere di creare una nuova sessione,
        diventando automaticamente il nuovo proprietario della partita se non lo era già.
        Il perdente, invece, è obbligato a lasciare la partita." */
    if (r->winner_fd != -1) {
        if (player_fd == r->winner_fd) {
            if (vote_yes) {
                /* Se il vincitore era il guest, diventa il nuovo owner */
                if (r->winner_fd == r->guest_fd) {
                    r->owner_fd = r->guest_fd;
                    strncpy(r->owner_name, r->guest_name, MAX_NAME_LEN);
                }

                /* Il perdente deve lasciare la stanza */
                int loser_fd = (r->winner_fd == r->owner_fd) ? r->guest_fd : r->owner_fd;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i].socket_fd == loser_fd) {
                        clients[i].active_room_id = -1;
                        break;
                    }
                }

                r->guest_fd = -1;
                r->guest_name[0] = '\0';
                r->pending_fd = -1;
                r->pending_name[0] = '\0';
                r->winner_fd = -1;
                r->rematch_owner_vote = 0;
                r->rematch_guest_vote = 0;
                r->state = STATE_WAITING;
                game_init(r->board);

                *out_new_session_ready = 1;
                pthread_mutex_unlock(&session_lock);

                /* Notifica broadcast che la stanza è tornata disponibile */
                char notif[BUFFER_SIZE];
                snprintf(notif, sizeof(notif), "ROOM_NOTIF %d IN_ATTESA %s\n", r->id, r->owner_name);
                broadcast_lobby(notif, r->owner_fd);

                return 1;
            } else {
                /* Il vincitore rifiuta: stanza chiusa */
                r->id = -1;
                pthread_mutex_unlock(&session_lock);
                return 0;
            }
        }
        pthread_mutex_unlock(&session_lock);
        return 0;
    }

    /* Caso Pareggio: opzionale da traccia
       "Nel caso in cui la partita termini in pareggio, entrambi i giocatori
        hanno la possibilità di scegliere congiuntamente se effettuare una nuova partita.
        la partita torna nello stato di attesa e il server invita nuovamente gli altri giocatori a partecipare." */
    if (r->winner_fd == -1) {
        if (player_fd == r->owner_fd) r->rematch_owner_vote = vote_yes ? 1 : -1;
        if (player_fd == r->guest_fd) r->rematch_guest_vote = vote_yes ? 1 : -1;

        if (r->rematch_owner_vote == 1 && r->rematch_guest_vote == 1) {
            /* Entrambi hanno concordato: nuova sessione in attesa */
            r->guest_fd = -1;
            r->guest_name[0] = '\0';
            r->pending_fd = -1;
            r->pending_name[0] = '\0';
            r->rematch_owner_vote = 0;
            r->rematch_guest_vote = 0;
            r->state = STATE_WAITING;
            game_init(r->board);
            if (out_owner_fd) *out_owner_fd = r->owner_fd;

            *out_new_session_ready = 1;
            pthread_mutex_unlock(&session_lock);

            char notif[BUFFER_SIZE];
            snprintf(notif, sizeof(notif), "ROOM_NOTIF %d IN_ATTESA %s\n", r->id, r->owner_name);
            broadcast_lobby(notif, r->owner_fd);

            return 2;
        } else if (r->rematch_owner_vote == -1 || r->rematch_guest_vote == -1) {
            /* Uno dei due ha rifiutato */
            pthread_mutex_unlock(&session_lock);
            return 0;
        }
    }

    pthread_mutex_unlock(&session_lock);
    return 0;
}

void room_close(int room_id) {
    pthread_mutex_lock(&session_lock);

    GameRoom *r = room_find_by_id(room_id);
    if (!r) {
        pthread_mutex_unlock(&session_lock);
        return;
    }

    /* Reset stato attivo per entrambi i client */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket_fd == r->owner_fd || clients[i].socket_fd == r->guest_fd) {
            if (clients[i].active_room_id == room_id) {
                clients[i].active_room_id = -1;
            }
        }
    }

    r->id = -1;
    r->state = STATE_CREATED;
    r->owner_fd = -1;
    r->guest_fd = -1;
    r->pending_fd = -1;

    pthread_mutex_unlock(&session_lock);

    char notif[BUFFER_SIZE];
    snprintf(notif, sizeof(notif), "ROOM_NOTIF %d CHIUSA Nessuno\n", room_id);
    broadcast_lobby(notif, -1);
}

void room_handle_disconnect(int socket_fd) {
    pthread_mutex_lock(&session_lock);

    for (int i = 0; i < MAX_ROOMS; i++) {
        if (rooms[i].id != -1) {
            if (rooms[i].owner_fd == socket_fd) {
                int other_fd = rooms[i].guest_fd;
                int rid = rooms[i].id;
                rooms[i].id = -1;
                pthread_mutex_unlock(&session_lock);

                if (other_fd != -1) {
                    send_raw(other_fd, "OPPONENT_DISCONNECTED\n");
                    client_set_active_room(other_fd, -1);
                }
                char notif[BUFFER_SIZE];
                snprintf(notif, sizeof(notif), "ROOM_NOTIF %d CHIUSA Nessuno\n", rid);
                broadcast_lobby(notif, -1);
                return;
            } else if (rooms[i].guest_fd == socket_fd) {
                int other_fd = rooms[i].owner_fd;
                int rid = rooms[i].id;
                rooms[i].id = -1;
                pthread_mutex_unlock(&session_lock);

                if (other_fd != -1) {
                    send_raw(other_fd, "OPPONENT_DISCONNECTED\n");
                    client_set_active_room(other_fd, -1);
                }
                char notif[BUFFER_SIZE];
                snprintf(notif, sizeof(notif), "ROOM_NOTIF %d CHIUSA Nessuno\n", rid);
                broadcast_lobby(notif, -1);
                return;
            } else if (rooms[i].pending_fd == socket_fd) {
                rooms[i].pending_fd = -1;
                rooms[i].pending_name[0] = '\0';
            }
        }
    }

    pthread_mutex_unlock(&session_lock);
}

