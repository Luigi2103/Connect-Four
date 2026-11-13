#ifndef SESSION_H
#define SESSION_H

#include <pthread.h>
#include "protocol.h"

#define MAX_CLIENTS 64
#define MAX_ROOMS 32

typedef struct {
    int socket_fd;
    char username[MAX_NAME_LEN];
    int active_room_id; /* -1 se non sta giocando attivamente */
} ConnectedClient;

typedef struct {
    int id;
    GameState state;
    
    int owner_fd;
    char owner_name[MAX_NAME_LEN];
    
    int guest_fd;
    char guest_name[MAX_NAME_LEN];

    int pending_fd;
    char pending_name[MAX_NAME_LEN];

    char board[BOARD_ROWS][BOARD_COLS];
    int current_turn_fd;
    int winner_fd; /* -1 = nessuno/pareggio */

    /* Flag per la gestione della rivincita */
    int rematch_owner_vote;
    int rematch_guest_vote;
} GameRoom;

/* Inizializzazione modulo sessioni */
void sessions_init(void);

/* Gestione Client */
int client_register(int socket_fd, const char *username);
void client_unregister(int socket_fd);
ConnectedClient* client_find_by_fd(int socket_fd);
int client_is_actively_playing(int socket_fd);
void client_set_active_room(int socket_fd, int room_id);

/* Gestione Stanze / Partite */
int room_create(int owner_fd);
GameRoom* room_find_by_id(int room_id);
int room_request_join(int room_id, int guest_fd);
int room_accept_join(int room_id, int owner_fd);
int room_reject_join(int room_id, int owner_fd);
int room_make_move(int room_id, int player_fd, int col, int *out_row, int *out_game_over, 
                   int *out_winner_fd, char *out_board_str, int *out_owner_fd, int *out_guest_fd, int *out_next_turn_fd);
int room_handle_rematch(int room_id, int player_fd, int vote_yes, int *out_new_session_ready, int *out_owner_fd);
void room_close(int room_id);
void room_handle_disconnect(int socket_fd);

/* Notifiche Broadcast */
void broadcast_lobby(const char *message, int exclude_fd);
void send_lobby_state(int target_fd);

#endif

