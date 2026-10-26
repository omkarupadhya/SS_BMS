CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -g

all: bank_server bank_client

bank_server: server.c database.c management.c database.h
	$(CC) $(CFLAGS) server.c database.c management.c -o bank_server

bank_client: client.c
	$(CC) $(CFLAGS) client.c -o bank_client

clean:
	rm -f bank_server bank_client

.PHONY: all clean
