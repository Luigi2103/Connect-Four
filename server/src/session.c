#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/socket.h>
#include "session.h"
#include "game.h"

static ConnectedClient clients[MAX_CLIENTS];
static GameRoom rooms[MAX_ROOMS];
static pthread_mutex_t session_lock;
static int next_room_id = 1;

static void room_init_slot(GameRoom *r, int id, GameState state, int owner_fd, const char *owner_name) {
    r->id = id;
    r->state = state;
    r->owner_fd = owner_fd;
    if (owner_name) {
        strncpy(r->owner_name, owner_name, MAX_NAME_LEN - 1);
        r->owner_name[MAX_NAME_LEN - 1] = '\0';
    } else {
        r->owner_name[0] = '\0';
    }
    r->guest_fd = -1;
    r->guest_name[0] = '\0';
    r->pending_fd = -1;
    r->pending_name[0] = '\0';
    r->current_turn_fd = -1;
    r->winner_fd = -1;
    r->rematch_owner_vote = 0;
    r->rematch_guest_vote = 0;
    game_init(r->board);
}

void sessions_init(void) {
    pthread_mutex_init(&session_lock, NULL);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].socket_fd = -1;
        clients[i].username[0] = '\0';
        clients[i].active_room_id = -1;
    }

    for (int i = 0; i < MAX_ROOMS; i++) {
        room_init_slot(&rooms[i], -1, STATE_CREATED, -1, NULL);
    }
}

int client_register(int socket_fd, const char *username) {
    pthread_mutex_lock(&session_lock);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket_fd != -1 && strcasecmp(clients[i].username, username) == 0) {
            pthread_mutex_unlock(&session_lock);
            return -1; /* Username già in uso (case-insensitive) */
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
            return -2; /* Client già impegnato in un'altra partita */
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
        return -1; /* Limite massimo stanze raggiunto */
    }

    ConnectedClient *cli = client_find_by_fd(owner_fd);
    if (!cli) {
        pthread_mutex_unlock(&session_lock);
        return -3; /* Client creatore non trovato */
    }

    GameRoom *r = &rooms[free_slot];
    room_init_slot(r, next_room_id++, STATE_WAITING, owner_fd, cli->username);

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

    /* Verifica se il guest ha già una richiesta pendente in un'altra stanza */
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (rooms[i].id != -1 && rooms[i].pending_fd == guest_fd) {
            pthread_mutex_unlock(&session_lock);
            return -7; /* Ha già una richiesta di sfida in attesa */
        }
    }

    GameRoom *r = room_find_by_id(room_id);
    if (!r) {
        pthread_mutex_unlock(&session_lock);
        return -2; /* Stanza non trovata */
    }

    /* Verifica se il creatore della stanza sta attualmente giocando un'altra partita */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket_fd == r->owner_fd && clients[i].active_room_id != -1) {
            pthread_mutex_unlock(&session_lock);
            return -8; /* Il proprietario è impegnato in gioco attivo */
        }
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
        return -6; /* Client sfidante non trovato */
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
        return -1; /* Stanza inesistente o chiamante non è il proprietario */
    }

    if (r->pending_fd == -1) {
        pthread_mutex_unlock(&session_lock);
        return -2; /* Nessuna richiesta di sfida pendente */
    }

    /* Verifica che lo sfidante sia ancora libero in lobby */
    int guest_still_free = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket_fd == r->pending_fd) {
            if (clients[i].active_room_id == -1) {
                guest_still_free = 1;
            }
            break;
        }
    }

    if (!guest_still_free) {
        /* Lo sfidante è già entrato in un'altra partita o si è disconnesso */
        r->pending_fd = -1;
        r->pending_name[0] = '\0';
        pthread_mutex_unlock(&session_lock);
        return -3; /* Sfidante non più disponibile */
    }

    r->guest_fd = r->pending_fd;
    strncpy(r->guest_name, r->pending_name, MAX_NAME_LEN);
    r->pending_fd = -1;
    r->pending_name[0] = '\0';

    /* Cancella eventuali altre richieste pendenti dello stesso giocatore */
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (rooms[i].id != -1 && rooms[i].id != room_id && rooms[i].pending_fd == r->guest_fd) {
            rooms[i].pending_fd = -1;
            rooms[i].pending_name[0] = '\0';
        }
    }

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
        return -1; /* Stanza inesistente o chiamante non è il proprietario */
    }

    if (r->pending_fd == -1) {
        pthread_mutex_unlock(&session_lock);
        return -2; /* Nessuna richiesta di sfida da rifiutare */
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
        return -1; /* Stanza inesistente o non in partita */
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

int room_handle_rematch(int room_id, int player_fd, int vote_yes, int *out_owner_fd, int *out_guest_fd) {
    pthread_mutex_lock(&session_lock);

    if (out_owner_fd) *out_owner_fd = -1;
    if (out_guest_fd) *out_guest_fd = -1;

    GameRoom *r = room_find_by_id(room_id);
    if (!r || r->state != STATE_FINISHED) {
        pthread_mutex_unlock(&session_lock);
        return -1; /* Stanza inesistente o partita non ancora conclusa */
    }

    if (player_fd != r->owner_fd && player_fd != r->guest_fd) {
        pthread_mutex_unlock(&session_lock);
        return -1; /* Non fa parte della partita */
    }

    /* Caso Vittoria: il vincitore può aprire una nuova sessione da proprietario; il perdente esce */
    if (r->winner_fd != -1) {
        if (player_fd == r->winner_fd) {
            if (vote_yes) {
                /* Se il vincitore era il guest, diventa il nuovo owner */
                if (r->winner_fd == r->guest_fd) {
                    r->owner_fd = r->guest_fd;
                    strncpy(r->owner_name, r->guest_name, MAX_NAME_LEN);
                }

                int loser_fd = (r->winner_fd == r->owner_fd) ? r->guest_fd : r->owner_fd;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i].socket_fd == loser_fd) {
                        clients[i].active_room_id = -1;
                        break;
                    }
                }

                room_init_slot(r, r->id, STATE_WAITING, r->owner_fd, r->owner_name);
                if (out_owner_fd) *out_owner_fd = r->owner_fd;

                pthread_mutex_unlock(&session_lock);

                /* Notifica broadcast che la stanza è tornata disponibile */
                char notif[BUFFER_SIZE];
                snprintf(notif, sizeof(notif), "ROOM_NOTIF %d IN_ATTESA %s\n", r->id, r->owner_name);
                broadcast_lobby(notif, r->owner_fd);

                return 1;
            } else {
                /* Il vincitore rifiuta: stanza chiusa */
                int closed_id = r->id;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i].socket_fd == player_fd) {
                        clients[i].active_room_id = -1;
                        break;
                    }
                }
                room_init_slot(r, -1, STATE_CREATED, -1, NULL);
                pthread_mutex_unlock(&session_lock);

                char notif[BUFFER_SIZE];
                snprintf(notif, sizeof(notif), "ROOM_NOTIF %d CHIUSA Nessuno\n", closed_id);
                broadcast_lobby(notif, -1);

                return 4;
            }
        }
        pthread_mutex_unlock(&session_lock);
        return -1;
    }

    /* Caso Pareggio: entrambi i giocatori votano se riaprire la stanza per una nuova partita */
    if (r->winner_fd == -1) {
        int orig_owner_fd = r->owner_fd;
        int orig_guest_fd = r->guest_fd;
        if (out_owner_fd) *out_owner_fd = orig_owner_fd;
        if (out_guest_fd) *out_guest_fd = orig_guest_fd;

        if (player_fd == r->owner_fd) r->rematch_owner_vote = vote_yes ? 1 : -1;
        if (player_fd == r->guest_fd) r->rematch_guest_vote = vote_yes ? 1 : -1;

        if (r->rematch_owner_vote == 1 && r->rematch_guest_vote == 1) {
            /* Entrambi hanno concordato: nuova sessione in attesa con owner originario */
            room_init_slot(r, r->id, STATE_WAITING, r->owner_fd, r->owner_name);

            /* Il guest torna libero in lobby */
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i].socket_fd == orig_guest_fd) {
                    clients[i].active_room_id = -1;
                    break;
                }
            }

            pthread_mutex_unlock(&session_lock);

            char notif[BUFFER_SIZE];
            snprintf(notif, sizeof(notif), "ROOM_NOTIF %d IN_ATTESA %s\n", r->id, r->owner_name);
            broadcast_lobby(notif, orig_owner_fd);

            return 2;
        } else if (r->rematch_owner_vote == -1 || r->rematch_guest_vote == -1) {
            /* Uno dei due ha rifiutato: chiudi la stanza per entrambi */
            int closed_id = r->id;
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i].socket_fd == orig_owner_fd || clients[i].socket_fd == orig_guest_fd) {
                    clients[i].active_room_id = -1;
                }
            }
            room_init_slot(r, -1, STATE_CREATED, -1, NULL);
            pthread_mutex_unlock(&session_lock);

            char notif[BUFFER_SIZE];
            snprintf(notif, sizeof(notif), "ROOM_NOTIF %d CHIUSA Nessuno\n", closed_id);
            broadcast_lobby(notif, -1);

            return 3;
        } else {
            /* Voto YES registrato, in attesa dell'altro */
            pthread_mutex_unlock(&session_lock);
            return 5;
        }
    }

    pthread_mutex_unlock(&session_lock);
    return 0;
}

int room_leave(int room_id, int client_fd) {
    pthread_mutex_lock(&session_lock);

    GameRoom *r = room_find_by_id(room_id);
    if (!r) {
        pthread_mutex_unlock(&session_lock);
        return -1; /* Stanza non trovata */
    }

    /* Solo il proprietario puo chiudere la stanza (o il guest se sta abbandonando una partita in corso) */
    if (r->owner_fd != client_fd) {
        if (r->state == STATE_IN_PROGRESS && r->guest_fd == client_fd) {
            /* Consentito: il guest abbandona la partita in corso */
        } else {
            pthread_mutex_unlock(&session_lock);
            return -2; /* Non puoi chiudere la stanza se non sei il proprietario */
        }
    }

    int owner_fd = r->owner_fd;
    int guest_fd = r->guest_fd;
    int was_in_progress = (r->state == STATE_IN_PROGRESS);
    int pending_fd = r->pending_fd;

    /* Reset stato attivo per entrambi i client */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket_fd == owner_fd || clients[i].socket_fd == guest_fd) {
            if (clients[i].active_room_id == room_id) {
                clients[i].active_room_id = -1;
            }
        }
    }

    room_init_slot(r, -1, STATE_CREATED, -1, NULL);

    pthread_mutex_unlock(&session_lock);

    /* Se c'era una richiesta pendente, notifica il richiedente */
    if (pending_fd != -1 && pending_fd != client_fd) {
        char rej[BUFFER_SIZE];
        snprintf(rej, sizeof(rej), "JOIN_REJECTED %d\n", room_id);
        send_raw(pending_fd, rej);
    }

    /* Se la partita era in corso, notifica soltanto l'avversario rimasto */
    if (was_in_progress) {
        int other_fd = (client_fd == owner_fd) ? guest_fd : owner_fd;
        if (other_fd != -1) {
            send_raw(other_fd, "OPPONENT_DISCONNECTED\n");
        }
    }

    char notif[BUFFER_SIZE];
    snprintf(notif, sizeof(notif), "ROOM_NOTIF %d CHIUSA Nessuno\n", room_id);
    broadcast_lobby(notif, -1);

    return 0;
}

void room_close(int room_id) {
    pthread_mutex_lock(&session_lock);

    GameRoom *r = room_find_by_id(room_id);
    if (!r) {
        pthread_mutex_unlock(&session_lock);
        return;
    }

    int owner_fd = r->owner_fd;
    int guest_fd = r->guest_fd;
    int was_in_progress = (r->state == STATE_IN_PROGRESS);
    int pending_fd = r->pending_fd;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket_fd == owner_fd || clients[i].socket_fd == guest_fd) {
            if (clients[i].active_room_id == room_id) {
                clients[i].active_room_id = -1;
            }
        }
    }

    room_init_slot(r, -1, STATE_CREATED, -1, NULL);

    pthread_mutex_unlock(&session_lock);

    if (pending_fd != -1) {
        char rej[BUFFER_SIZE];
        snprintf(rej, sizeof(rej), "JOIN_REJECTED %d\n", room_id);
        send_raw(pending_fd, rej);
    }

    if (was_in_progress) {
        if (owner_fd != -1) send_raw(owner_fd, "OPPONENT_DISCONNECTED\n");
        if (guest_fd != -1) send_raw(guest_fd, "OPPONENT_DISCONNECTED\n");
    }

    char notif[BUFFER_SIZE];
    snprintf(notif, sizeof(notif), "ROOM_NOTIF %d CHIUSA Nessuno\n", room_id);
    broadcast_lobby(notif, -1);
}

void room_handle_disconnect(int socket_fd) {
    int notify_other_fds[MAX_ROOMS];
    int notify_pending_fds[MAX_ROOMS];
    int closed_rids[MAX_ROOMS];
    int count = 0;

    pthread_mutex_lock(&session_lock);

    for (int i = 0; i < MAX_ROOMS; i++) {
        if (rooms[i].id != -1) {
            if (rooms[i].owner_fd == socket_fd) {
                notify_other_fds[count] = rooms[i].guest_fd;
                notify_pending_fds[count] = rooms[i].pending_fd;
                closed_rids[count] = rooms[i].id;
                rooms[i].id = -1;
                count++;
            } else if (rooms[i].guest_fd == socket_fd) {
                notify_other_fds[count] = rooms[i].owner_fd;
                notify_pending_fds[count] = -1;
                closed_rids[count] = rooms[i].id;
                rooms[i].id = -1;
                count++;
            } else if (rooms[i].pending_fd == socket_fd) {
                rooms[i].pending_fd = -1;
                rooms[i].pending_name[0] = '\0';
            }
        }
    }

    pthread_mutex_unlock(&session_lock);

    /* Notifica i giocatori rimasti e la lobby per ogni stanza chiusa */
    for (int i = 0; i < count; i++) {
        if (notify_other_fds[i] != -1) {
            send_raw(notify_other_fds[i], "OPPONENT_DISCONNECTED\n");
            client_set_active_room(notify_other_fds[i], -1);
        }
        if (notify_pending_fds[i] != -1) {
            char rej[BUFFER_SIZE];
            snprintf(rej, sizeof(rej), "JOIN_REJECTED %d\n", closed_rids[i]);
            send_raw(notify_pending_fds[i], rej);
        }
        char notif[BUFFER_SIZE];
        snprintf(notif, sizeof(notif), "ROOM_NOTIF %d CHIUSA Nessuno\n", closed_rids[i]);
        broadcast_lobby(notif, -1);
    }
}

