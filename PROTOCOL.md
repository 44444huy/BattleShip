# Battleship TCP protocol

Each command/response is an ASCII text line ending in LF. CRLF is accepted.
The maximum payload is 255 bytes before LF. TCP chunks are not messages:
clients must buffer until newline and handle multiple lines per read.
An oversized/control-character-containing request is rejected and discarded
through its next newline; the following request can still be processed.

Command names are case-sensitive. Coordinates are A1–J10 (row letter, column
number), with lowercase letters accepted. Directions are H or V. Arguments
are whitespace-separated and cannot contain spaces. Errors use `ERROR <text>`.
The server can send unsolicited events at any time, including between command
responses. A client must dispatch by message type, not assume one response
per command. Server output is nonblocking and bounded; a peer that cannot keep
up is disconnected rather than blocking other players or their timers.

## Connection and accounts

| Request | Success response |
| --- | --- |
| Connect | `CONNECTED` |
| `HELLO` | `WELCOME` |
| `PING` | `PONG` |
| `REGISTER <username> <password>` | `REGISTER_OK` |
| `LOGIN <username> <password>` | `LOGIN_OK <username>` |
| `LOGOUT` | `LOGOUT_OK` |
| `QUIT` | `BYE`, then connection closes |

Usernames contain 1–31 ASCII letters, digits or underscores. Passwords contain
1–63 non-whitespace characters. Registration does not log in. Only one live
session can log in to an account. Except HELLO/PING/QUIT and registration/login,
all commands require authentication. Logout/quit/disconnect while in a match
finalizes a loss; a server shutdown/restart aborts instead.

## Lobby and matchmaking

`READY` / `UNREADY` respond `READY_OK` / `UNREADY_OK`. Only logged-in players
outside matches can queue. READY is idempotent: it never resets queue age.
The oldest queued player chooses the closest-rated candidate within its own
age-based gap (100/150/200 after 0/10/20 seconds). Ties use candidate queue
entry order. The newer candidate need not have reached the older player's gap.
Both leave the queue before another pair is considered. No pair may exceed
200 points, however long either has waited. UNREADY does not cancel invitations.

```text
LIST_ONLINE -> ONLINE_LIST <count>
               ONLINE_PLAYER <username> <ONLINE|READY|IN_GAME>  (count lines)
               ONLINE_LIST_END
LIST_READY -> READY_LIST <count>
              READY_PLAYER <username>  (count lines)
              READY_LIST_END
LIST_CHALLENGES -> CHALLENGE_LIST <count>
                   CHALLENGER <username>  (count lines)
                   CHALLENGE_LIST_END
```

Lists include the requester if eligible. LIST_CHALLENGES lists incoming offers.

| Request | Events |
| --- | --- |
| `CHALLENGE <username>` | Sender: `CHALLENGE_SENT <username>`; recipient: `CHALLENGE_FROM <sender>` |
| `ACCEPT_CHALLENGE <username>` | Both enter the common match-start flow |
| `DECLINE_CHALLENGE <username>` | Recipient: `DECLINE_CHALLENGE_OK <username>`; sender: `CHALLENGE_DECLINED <recipient>` |
| `REMATCH` | Sender: `REMATCH_SENT <opponent>`; recipient: `REMATCH_REQUEST <sender>` |
| `ACCEPT_REMATCH` | Both enter the common match-start flow |
| `DECLINE_REMATCH` | Recipient: `DECLINE_REMATCH_OK <opponent>`; sender: `REMATCH_DECLINED <recipient>` |

Rematches require both previous opponents still online and available. Challenges
do not require queuing. Sending a successful challenge/rematch request removes
its sender from the queue. Entering a match cancels outstanding invitations,
notifying affected peers with `CHALLENGE_CANCELLED <username>` or
`REMATCH_CANCELLED <username>`. An incoming challenge does not stop automatic
matchmaking. Logging out/disconnecting also cancels associated invitations.

## Match start and full-fleet placement

Every source uses the same start sequence. Matchmaking first sends
`MATCH_FOUND <opponent>`. Challenge/rematch clients already know the opponent.

```text
MATCH_START
FLEET_BEGIN 5 120000
FLEET_SHIP <id> <name> <length>  (five lines)
FLEET_END
```

MATCH_START has no arguments. The placement limit is milliseconds. Ship IDs
1–5 have lengths 5,4,3,3,2. Send exactly one triple per ship, in any order:

```text
PLACE_FLEET 1 A1 H 2 C1 H 3 E1 H 4 G1 H 5 I1 H
```

The server atomically validates the whole fleet: distinct IDs, all ships,
valid coordinates/directions, in bounds and no overlap. Ships may touch.
Success is `FLEET_READY`; invalid input leaves the board and deadline unchanged.
An accepted fleet cannot be replaced.

At the 120-second deadline, each missing fleet is generated in full. Only its
owner receives `AUTO_FLEET`, five `OWN_SHIP <id> <start> <H|V> <length>` lines,
an OWN board snapshot and `FLEET_READY`. The opponent receives no geometry.
No placement timeout is a loss. Once both fleets are available, both receive
`GAME_READY`, followed by their turn notification. The challenger/rematch
requester, or the matchmaking anchor (older queued player), moves first.

## Boards and shots

`SHOW_BOARD` and `SHOW_OPPONENT` return:

```text
BOARD_BEGIN <OWN|OPPONENT>
BOARD_ROW A <10 cells>
... rows B through J ...
BOARD_END
```

Cells: `.` unknown/water, `S` own unhit ship, `X` hit, `O` miss. OPPONENT
snapshots never expose `S`. Send `SHOOT <coordinate>` only on your turn:

- Shooter: `SHOT_RESULT <coordinate> <HIT|MISS>`.
- Defender: `OPPONENT_SHOT <coordinate> <HIT|MISS>`.
- On sinking: shooter additionally receives `SHIP_SUNK <id> <start> <H|V> <length>`;
  defender receives `YOUR_SHIP_SUNK <id>`.
- A valid hit keeps the same player; a miss transfers the turn. Either starts
  a fresh 30 seconds. Repeated/out-of-bounds shots are errors, with no reset.

Turn messages are `YOUR_TURN <remaining_ms>` and
`OPPONENT_TURN <remaining_ms>`. On expiry both receive
`TURN_TIMEOUT <username>`, then new turn messages. No shot is made and no
forfeit occurs. Deadlines use a monotonic server clock; expiry is processed
before a command received at or after its deadline. Client countdowns are only
displays of these server-controlled timers, not an authority to extend time.

## Pause, draw and final result

After placement, either player may send `PAUSE`; both receive
`GAME_PAUSED <username> <remaining_ms>`. Only that player can `RESUME`, which
sends `GAME_RESUMED <username>` and restored turn messages. Pause is unlimited.

`DRAW` sends `DRAW_OFFER_SENT` to its sender and `DRAW_OFFER <username>` to
the opponent. It freezes the remaining turn time, including while a modal GUI
dialog is open. Only the opponent can respond:

- `DECLINE_DRAW`: both get `DRAW_DECLINED <username>`, then restored turn
  messages unless the game is still paused.
- `ACCEPT_DRAW`: both get the final draw result.

A second draw cannot be offered while one is pending. A draw can be offered
while paused; resuming does not unfreeze a still-pending draw, and declining
does not unfreeze an existing pause. New pauses are rejected while a draw is
pending. `RESIGN` works even during placement or suspension.

```text
GAME_RESULT <WIN|LOSE|DRAW> <reason> <match_id> <new_rating> <rating_delta>
```

Reasons: `ALL_SHIPS_SUNK`, `RESIGN`, `DRAW_ACCEPTED`, `DISCONNECT`, `LOGOUT`.
This message is sent only after the outcome, final event and both rating
updates commit together. New accounts start at 1000; matchmaking uses Elo
expected score `1/(1+10^((opponent_rating-rating)/400))`, K=32, with nearest
integer deltas and opposite deltas for the two players. Challenges/rematches
have zero delta. Win/loss/draw stats include all completed sources.

`MATCH_ABORTED <match_id> <SERVER_ERROR|SERVER_SHUTDOWN>` means no result/Elo
was awarded. Storage errors abort the live session without acknowledging the
unsaved action. If storage remains unavailable, restart recovery aborts any
remaining ACTIVE records. Aborted games are excluded from completed history,
stats and replay. Shutdown notification delivery is best effort as sockets close.

## Statistics, leaderboard and history

```text
STATS
-> STATS <rating> <wins> <losses> <draws>

LEADERBOARD <offset>
-> LEADERBOARD_BEGIN <count>
   RANK <1-based-position> <username> <rating>
   LEADERBOARD_END <next_offset|-1>

HISTORY <offset>
-> HISTORY_BEGIN <count>
   HISTORY_ITEM <match_id> <start_epoch_seconds> <opponent> <source> <WIN|LOSE|DRAW> <reason> <rating_delta>
   HISTORY_END <next_offset|-1>
```

Offsets are zero-based nonnegative integers. Pages have at most 20 entries.
Leaderboard includes all accounts, rating descending then username ascending.
History contains only the requester's completed matches, newest ID first.
Source is `MATCHMAKING`, `CHALLENGE` or `REMATCH`. All lines are bounded by
the normal message limit. Fetch the next page only if the end cursor is not -1.

## Participant-only replay

```text
REPLAY <match_id> [after_sequence]
-> REPLAY_BEGIN <match_id> <player0> <player1> <source> <winner_index> <reason>
   REPLAY_EVENT <sequence> <elapsed_ms> <payload>
   REPLAY_END <next_after_sequence|-1>
```

Winner index is 0/1 or -1 for a draw. Omit the cursor (equivalent to 0) to start.
Each page returns up to 100 events, with a repeated header. If the end cursor
is nonnegative, request `REPLAY <match_id> <cursor>` to continue after that
sequence; -1 means complete. Pagination bounds memory/output even for games
with unlimited pauses/timeouts. It does not discard events. Fetch all pages
before treating a replay as complete. Only participants can fetch finished
matches; live, aborted, nonexistent and unauthorized requests all return
`ERROR Replay unavailable`.

Events have increasing sequence numbers; elapsed milliseconds are measured
from match creation. Payloads are:

| Payload | Replay interpretation |
| --- | --- |
| `MATCH_START` | Initialize two empty boards |
| `SHIP <player> <id> <start> <H|V> <length> <MANUAL|AUTO>` | Set initial ship geometry; ten records if setup completes |
| `GAME_READY <first_player>` | Begin shooting with 30000 ms |
| `SHOT <shooter> <cell> <HIT|MISS>` | Apply hit/miss to the other board; hit retains turn, miss switches |
| `SUNK <owner> <id> <start> <H|V> <length>` | Mark that owner's ship sunk |
| `TIMEOUT <player>` | Transfer turn, change no cells, reset to 30000 ms |
| `PAUSE <player>`, `RESUME <player>` | Suspend/restore remaining time |
| `DRAW_OFFER <player>`, `DRAW_DECLINED <player>` | Suspend/restore for draw negotiation |
| `RESULT <winner_index> <reason> <delta0> <delta1>` | Final outcome; DRAW_ACCEPTED records draw acceptance |

Player indices always refer to the header's player0/player1, not the viewer.
A resignation/disconnect during placement can finish with fewer than ten SHIP
records; a replay must render the submitted fleets without inventing missing
ones. Invalid actions are neither applied nor logged.
