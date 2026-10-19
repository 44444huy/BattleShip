CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L
LDLIBS_SERVER = -lsqlite3 -lcrypto -lm
TARGET = battleship
TEST_TARGET = test_game
ACCOUNT_TEST_TARGET = test_account
SERVER_TARGET = server
CLIENT_TARGET = client
SOURCES = main.c game.c
SERVER_CORE = server_state.c server_game.c server_commands.c server_queries.c \
	server_connection.c storage.c account.c fleet.c game.c
SERVER_SOURCES = server.c $(SERVER_CORE)

all: $(TARGET) $(SERVER_TARGET) $(CLIENT_TARGET)

$(TARGET): $(SOURCES) game.h
	$(CC) $(CFLAGS) $(SOURCES) -o $(TARGET)

$(SERVER_TARGET): $(SERVER_SOURCES) server.h protocol.h account.h storage.h game.h
	$(CC) $(CFLAGS) $(SERVER_SOURCES) -o $(SERVER_TARGET) $(LDLIBS_SERVER)

$(CLIENT_TARGET): client.c protocol.c protocol.h
	$(CC) $(CFLAGS) client.c protocol.c -o $(CLIENT_TARGET)

test: $(TEST_TARGET) $(ACCOUNT_TEST_TARGET) test_server
	./$(TEST_TARGET)
	./$(ACCOUNT_TEST_TARGET)
	./test_server

$(TEST_TARGET): test_game.c game.c game.h
	$(CC) $(CFLAGS) test_game.c game.c -o $(TEST_TARGET)

$(ACCOUNT_TEST_TARGET): test_account.c account.c storage.c account.h storage.h
	$(CC) $(CFLAGS) test_account.c account.c storage.c -o $(ACCOUNT_TEST_TARGET) $(LDLIBS_SERVER)

test_server: test_server.c $(SERVER_CORE) server.h storage.h account.h game.h
	$(CC) $(CFLAGS) test_server.c $(SERVER_CORE) -o $@ $(LDLIBS_SERVER)

integration: server
	python3 tests/test_protocol.py ./server

clean:
	rm -f $(TARGET) $(TEST_TARGET) $(ACCOUNT_TEST_TARGET) \
		$(SERVER_TARGET) $(CLIENT_TARGET) test_server

.PHONY: all test integration clean
