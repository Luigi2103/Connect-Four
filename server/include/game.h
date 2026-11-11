#ifndef GAME_H
#define GAME_H

#include "protocol.h"

void game_init(char board[BOARD_ROWS][BOARD_COLS]);
int game_is_valid_move(char board[BOARD_ROWS][BOARD_COLS], int col);
int game_drop_piece(char board[BOARD_ROWS][BOARD_COLS], int col, char piece);
int game_check_win(char board[BOARD_ROWS][BOARD_COLS], char piece);
int game_is_full(char board[BOARD_ROWS][BOARD_COLS]);
void game_serialize(char board[BOARD_ROWS][BOARD_COLS], char *dest);

#endif

