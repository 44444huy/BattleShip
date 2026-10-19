# Battleship

A two-player Battleship game written in C with a TCP client-server model.

## Build

Build in Linux or WSL. On Debian/Ubuntu, install the compiler, SQLite and
OpenSSL development libraries first:

```sh
sudo apt-get install build-essential libsqlite3-dev libssl-dev python3
```

Then run:

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
make integration
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
creates `battleship.db` automatically for accounts, ratings, matches and replay
events. An optional second argument selects a different database:

```sh
./server 8080 /path/to/battleship.db
```

Existing `accounts.txt` is left untouched and is **not loaded or migrated**;
register new accounts. Passwords are stored as independently salted
PBKDF2-HMAC-SHA256 hashes. The TCP connection is still unencrypted: use only
test passwords on trusted networks until TLS is added.

Only one server may use a database at a time. On restart, unfinished matches
are marked aborted, with no Elo or completed-game statistics. Ctrl+C shuts
down cleanly and aborts active matches rather than penalizing players.

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
ACCEPT_CHALLENGE <username>
DECLINE_CHALLENGE <username>
```

Game commands:

```text
PLACE_FLEET 1 A1 H 2 C1 H 3 E1 H 4 G1 H 5 I1 H
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

Persistent information commands (login required):

```text
STATS
LEADERBOARD <offset>
HISTORY <offset>
REPLAY <match_id> [after_sequence]
```

Start history/leaderboard at offset `0`. Replay is available only to the
participants of a completed match. Follow the cursor in `REPLAY_END` when
it is not `-1`. See [PROTOCOL.md](PROTOCOL.md) for exact response formats.

## Selected game rules

- `READY` enters ranked matchmaking; `UNREADY` leaves. Repeated `READY`
  preserves the original waiting time and priority.
- Matchmaking considers the oldest waiting player first and chooses the
  closest-rated eligible opponent, breaking ties by queue-entry order.
  The permitted difference is 100 points for the first 10 seconds, 150
  for seconds 10–19, and 200 thereafter. It never exceeds 200. A player
  can wait indefinitely at the cap; checks continue at least once a second.
- Direct challenges do not need `READY`. Sending one leaves the sender's
  queue. Accepting one removes both players and cancels conflicting requests.
  A queued recipient can instead be matched automatically, cancelling the
  challenge and notifying its sender.
- New accounts start at 1000 Elo; K=32. Only matchmaking changes ratings.
  Challenges and rematches are unranked, but all completed games count in stats.
  Resignation, logout and live disconnect are losses, including during setup.
- Every match sends the entire fleet specification at once. Submit all five
  ships in one `PLACE_FLEET` line, in any ship-ID order. The server validates
  every ship, coordinate, direction and overlap before accepting any of them.
- Setup lasts at most 120 seconds. Missing fleets are randomly generated in
  full using shuffled candidate placements and backtracking. This never
  forfeits a match. A previously accepted fleet is preserved; an unsubmitted
  GUI arrangement exists only on the client and is discarded on expiry.
- Each turn lasts 30 seconds. Hits keep the turn, misses transfer it; both
  start a fresh timer. Invalid moves do not reset it. Expiry transfers the
  turn without firing or forfeiting, regardless of prior timeouts.
- Either player can pause once setup is complete, without a usage limit;
  only the player who paused can resume. A pending draw offer also freezes
  the remaining turn time. Declining a draw/resuming restores that remainder
  when no other suspension remains. These rules can keep a match open forever.
- The server owns both boards. Live updates reveal only shots and sunk-ship
  geometry, not an opponent's surviving ships. Complete fleets are available
  in participant-only replays after completion.

The durations are currently server constants (`PLACEMENT_MS`, `TURN_MS` in
`server.h`), not per-player configurable settings. JavaFX and client-side
countdown displays are the next phase. Rating only matchmaking is intentionally
narrower than the rubric's wording about updates after every match.

## Implementation map

| File | Responsibility |
| --- | --- |
| `server.c` | Nonblocking `select()` loop, deadlines, shutdown and database lock |
| `server.h`, `server_state.c` | Shared context, clients, queue, matchmaking and cleanup |
| `server_connection.c` | TCP framing, bounded output buffers and partial writes |
| `server_commands.c` | Authentication, lobby, challenges and command dispatch |
| `server_game.c` | Shared match lifecycle, fleet submission, actions and timers |
| `fleet.c` | Randomized backtracking fleet generator |
| `storage.c`, `storage.h` | SQLite schema, events, transactions, Elo and recovery |
| `account.c`, `account.h` | Account registration and salted password verification |
| `server_queries.c` | History, replay, stats and leaderboard |
| `game.c`, `game.h` | Board rules, placement validation and shot outcomes |
| `client.c`, `protocol.c` | Terminal client and its line I/O helpers |

Match events are committed before actions are acknowledged. Final outcome and
both rating updates commit together, once per match. If a match write fails,
the server aborts that session without awarding Elo; if even the abort cannot
be saved, restart recovery marks its remaining active record aborted.

## Verification

`make test` covers board rules, salted accounts, atomic Elo updates, restart
recovery, matchmaking thresholds/priority, challenge interactions, fleet
validation/generation, timers, pagination, access restrictions and injected
storage failures. Server tests inject a monotonic clock and deterministic
random seeds, so minute-long deadlines are exercised instantly.

`make integration` uses real TCP sockets and a temporary SQLite database to
check framing, hidden boards, gameplay, ranked disconnects, persistent replays,
database locking and recovery after killing the server. It never uses the
normal `battleship.db` or `accounts.txt`.
