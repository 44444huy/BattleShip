# Battleship

A two-player Battleship game written in C with a TCP client-server model.

## Build

Run the following command in Linux or WSL:

```sh
make
```

This builds:

- `battleship`: local two-player version
- `server`: TCP game server
- `client`: terminal TCP client

To compile and run the tests:

```sh
make test
```

## Run The Online Game

Start the server first. The port must be from 1 to 65535.

```sh
./server 8080
```

Open a second terminal for Player 1:

```sh
./client 127.0.0.1 8080
```

Open a third terminal for Player 2:

```sh
./client 127.0.0.1 8080
```

Use `127.0.0.1` when all programs run on the same computer. The server
creates `accounts.txt` automatically to store registered accounts.

## Commands

Account commands:

```text
REGISTER <username> <password>
LOGIN <username> <password>
LOGOUT
```

Lobby and challenge commands:

```text
READY
UNREADY
LIST_READY
LIST_ONLINE
LIST_CHALLENGES
CHALLENGE <username>
ACCEPT <username>
DECLINE <username>
```

Game commands:

```text
PLACE <ship 1-5> <A1-J10> <H|V>
SHOW_BOARD
SHOW_OPPONENT
SHOOT <A1-J10>
PAUSE
RESUME
DRAW
ACCEPT_DRAW
DECLINE_DRAW
RESIGN
```

After a game finishes, the previous opponents can use:

```text
REMATCH
ACCEPT_REMATCH
DECLINE_REMATCH
```

Connection commands:

```text
HELLO
PING
QUIT
```

Each command is one text line. The client automatically adds the newline
required by the server protocol.
