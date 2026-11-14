CC = gcc
CFLAGS = -Wall -Wextra -pthread -Iinclude -std=c99
SRC = src/main.c src/game.c src/session.c src/client_handler.c
TARGET = server_forza4

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

tsan: clean
	$(CC) $(CFLAGS) -fsanitize=thread -g $(SRC) -o $(TARGET)

asan: clean
	$(CC) $(CFLAGS) -fsanitize=address,undefined -g $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean tsan asan
