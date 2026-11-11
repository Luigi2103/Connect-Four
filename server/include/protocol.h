#ifndef PROTOCOL_H
#define PROTOCOL_H

#define DEFAULT_PORT 5050
#define BUFFER_SIZE 1024
#define MAX_NAME_LEN 32

/* Dimensioni griglia Forza 4 */
#define BOARD_ROWS 6
#define BOARD_COLS 7

/* Simboli celle */
#define CELL_EMPTY ' '
#define CELL_P1 'X'
#define CELL_P2 'O'

/* Stati della partita */
typedef enum {
    STATE_CREATED,
    STATE_WAITING,
    STATE_IN_PROGRESS,
    STATE_FINISHED
} GameState;

#endif

