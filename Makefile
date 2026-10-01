CC = gcc
CFLAGS = -Wall -Wextra -std=c11
TARGET = battleship
TEST_TARGET = test_game
ACCOUNT_TEST_TARGET = test_account
SERVER_TARGET = server
CLIENT_TARGET = client
SOURCES = main.c game.c

all: $(TARGET) $(SERVER_TARGET) $(CLIENT_TARGET)

$(TARGET): $(SOURCES) game.h
	$(CC) $(CFLAGS) $(SOURCES) -o $(TARGET)

$(SERVER_TARGET): server.c protocol.c protocol.h account.c account.h game.c game.h
	$(CC) $(CFLAGS) server.c protocol.c account.c game.c -o $(SERVER_TARGET)

$(CLIENT_TARGET): client.c protocol.c protocol.h
	$(CC) $(CFLAGS) client.c protocol.c -o $(CLIENT_TARGET)

test: $(TEST_TARGET) $(ACCOUNT_TEST_TARGET)
	./$(TEST_TARGET)
	./$(ACCOUNT_TEST_TARGET)

$(TEST_TARGET): test_game.c game.c game.h
	$(CC) $(CFLAGS) test_game.c game.c -o $(TEST_TARGET)

$(ACCOUNT_TEST_TARGET): test_account.c account.c account.h
	$(CC) $(CFLAGS) test_account.c account.c -o $(ACCOUNT_TEST_TARGET)

clean:
	rm -f $(TARGET) $(TEST_TARGET) $(ACCOUNT_TEST_TARGET) \
		$(SERVER_TARGET) $(CLIENT_TARGET) test_accounts.tmp

.PHONY: all test clean
