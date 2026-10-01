CC = gcc
CFLAGS = -Wall -Wextra -std=c11
TARGET = battleship
TEST_TARGET = test_game
SERVER_TARGET = server
CLIENT_TARGET = client
SOURCES = main.c game.c

all: $(TARGET) $(SERVER_TARGET) $(CLIENT_TARGET)

$(TARGET): $(SOURCES) game.h
	$(CC) $(CFLAGS) $(SOURCES) -o $(TARGET)

$(SERVER_TARGET): server.c protocol.c protocol.h
	$(CC) $(CFLAGS) server.c protocol.c -o $(SERVER_TARGET)

$(CLIENT_TARGET): client.c protocol.c protocol.h
	$(CC) $(CFLAGS) client.c protocol.c -o $(CLIENT_TARGET)

test: $(TEST_TARGET)
	./$(TEST_TARGET)

$(TEST_TARGET): test_game.c game.c game.h
	$(CC) $(CFLAGS) test_game.c game.c -o $(TEST_TARGET)

clean:
	rm -f $(TARGET) $(TEST_TARGET) $(SERVER_TARGET) $(CLIENT_TARGET)

.PHONY: all test clean
