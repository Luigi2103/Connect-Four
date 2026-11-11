#include <string.h>
#include "game.h"

/* Prototipi funzioni ausiliarie private */
static int check_horizontal(char board[BOARD_ROWS][BOARD_COLS], char piece);
static int check_vertical(char board[BOARD_ROWS][BOARD_COLS], char piece);
static int check_diagonal_down(char board[BOARD_ROWS][BOARD_COLS], char piece);
static int check_diagonal_up(char board[BOARD_ROWS][BOARD_COLS], char piece);

void game_init(char board[BOARD_ROWS][BOARD_COLS]) {
    for (int r = 0; r < BOARD_ROWS; r++) {
        for (int c = 0; c < BOARD_COLS; c++) {
            board[r][c] = CELL_EMPTY;
        }
    }
}

int game_is_valid_move(char board[BOARD_ROWS][BOARD_COLS], int col) {
    if (col < 0 || col >= BOARD_COLS) {
        return 0;
    }
    return (board[0][col] == CELL_EMPTY);
}

int game_drop_piece(char board[BOARD_ROWS][BOARD_COLS], int col, char piece) {
    if (!game_is_valid_move(board, col)) {
        return -1;
    }
    for (int r = BOARD_ROWS - 1; r >= 0; r--) {
        if (board[r][col] == CELL_EMPTY) {
            board[r][col] = piece;
            return r;
        }
    }
    return -1;
}

int game_check_win(char board[BOARD_ROWS][BOARD_COLS], char piece) {
    return check_horizontal(board, piece) ||
           check_vertical(board, piece)   ||
           check_diagonal_down(board, piece) ||
           check_diagonal_up(board, piece);
}

int game_is_full(char board[BOARD_ROWS][BOARD_COLS]) {
    for (int c = 0; c < BOARD_COLS; c++) {
        if (board[0][c] == CELL_EMPTY) {
            return 0;
        }
    }
    return 1;
}

void game_serialize(char board[BOARD_ROWS][BOARD_COLS], char *dest) {
    int idx = 0;
    for (int r = 0; r < BOARD_ROWS; r++) {
        for (int c = 0; c < BOARD_COLS; c++) {
            char ch = board[r][c];
            dest[idx++] = (ch == CELL_EMPTY) ? '.' : ch;
        }
    }
    dest[idx] = '\0';
}

// funzioni ausiliarie

static int check_horizontal(char board[BOARD_ROWS][BOARD_COLS], char piece) {
    for (int r = 0; r < BOARD_ROWS; r++) {
        for (int c = 0; c <= BOARD_COLS - 4; c++) { //dalla colonna 4 in poi non posso più avere la combinazione vincente
            if (board[r][c] == piece &&
                board[r][c + 1] == piece &&
                board[r][c + 2] == piece &&
                board[r][c + 3] == piece) {
                return 1;
            }
        }
    }
    return 0;
}

static int check_vertical(char board[BOARD_ROWS][BOARD_COLS], char piece) {
    for (int c = 0; c < BOARD_COLS; c++) {
        for (int r = 0; r <= BOARD_ROWS - 4; r++) { //dalla riga 4 in poi non posso più avere la combinazione vincente
            if (board[r][c] == piece &&
                board[r + 1][c] == piece &&
                board[r + 2][c] == piece &&
                board[r + 3][c] == piece) {
                return 1;
            }
        }
    }
    return 0;
}

//controllo sulla diagonale che va da in alto a sx verso in basso a dx
static int check_diagonal_down(char board[BOARD_ROWS][BOARD_COLS], char piece) {
    for (int r = 0; r <= BOARD_ROWS - 4; r++) { //dalla riga 4 in poi non posso più avere la combinazione vincente
        for (int c = 0; c <= BOARD_COLS - 4; c++) { //dalla colonna 4 in poi non posso più avere la combinazione vincente
            if (board[r][c] == piece &&
                board[r + 1][c + 1] == piece &&
                board[r + 2][c + 2] == piece &&
                board[r + 3][c + 3] == piece) {
                return 1;
            }
        }
    }
    return 0;
}

//controllo sulla diagonale che va da in basso a sx verso in alto a dx
static int check_diagonal_up(char board[BOARD_ROWS][BOARD_COLS], char piece) {
    for (int r = 3; r < BOARD_ROWS; r++) { //dalla riga 3 in poi non posso più avere la combinazione vincente
        for (int c = 0; c <= BOARD_COLS - 4; c++) { //dalla colonna 4 in poi non posso più avere la combinazione vincente
            if (board[r][c] == piece &&
                board[r - 1][c + 1] == piece &&
                board[r - 2][c + 2] == piece &&
                board[r - 3][c + 3] == piece) {
                return 1;
            }
        }
    }
    return 0;
}