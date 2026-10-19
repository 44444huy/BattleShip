"""Real TCP/restart tests; uses a temporary database, never the player's database."""
import pathlib
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time


class Peer:
    def __init__(self, port):
        self.socket = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.pending = b""
        assert self.line() == "CONNECTED"

    def line(self):
        while b"\n" not in self.pending:
            data = self.socket.recv(4096)
            assert data, "Unexpected server disconnect"
            self.pending += data
        line, self.pending = self.pending.split(b"\n", 1)
        assert len(line) < 256
        return line.decode("ascii")

    def until(self, prefix):
        lines = []
        for _ in range(300):
            line = self.line()
            lines.append(line)
            if line == prefix or line.startswith(prefix + " "):
                return lines
        raise AssertionError(f"No {prefix!r} in {lines!r}")

    def send(self, message):
        self.socket.sendall((message + "\n").encode("ascii"))

    def login(self, name, register=True):
        if register:
            self.send(f"REGISTER {name} secret")
            assert self.line() == "REGISTER_OK"
        self.send(f"LOGIN {name} secret")
        assert self.line() == f"LOGIN_OK {name}"

    def close(self):
        self.socket.close()


def port_number():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def start(binary, port, db):
    process = subprocess.Popen([binary, str(port), str(db)], stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True)
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        assert process.poll() is None, process.communicate()
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=.1):
                return process
        except OSError:
            time.sleep(.02)
    process.kill()
    raise AssertionError("Server did not start")


def stop(process, crash=False):
    if process.poll() is None:
        process.kill() if crash else process.terminate()
    process.communicate(timeout=5)
    if not crash:
        assert process.returncode == 0


def run(binary):
    fleet = "PLACE_FLEET 1 A1 H 2 C1 H 3 E1 H 4 G1 H 5 I1 H"
    with tempfile.TemporaryDirectory(prefix="battleship-test-") as directory:
        db = pathlib.Path(directory) / "test.db"
        port = port_number()
        process = start(binary, port, db)
        peers = []
        try:
            a, b, outsider = [Peer(port) for _ in range(3)]
            peers += [a, b, outsider]
            # A fragmented command and several lines in one TCP write.
            a.socket.sendall(b"PI")
            a.socket.sendall(b"NG\r\nHELLO\n")
            assert a.line() == "PONG" and a.line() == "WELCOME"
            a.socket.sendall(b"X" * 300 + b"\nPING\n")
            assert a.line().startswith("ERROR") and a.line() == "PONG"
            a.socket.sendall(b"PING\x00ignored\nPING\n")
            assert a.line().startswith("ERROR") and a.line() == "PONG"
            a.login("alice")
            b.login("bob")
            outsider.login("charlie")
            a.send("READY")
            a.until("READY_OK")
            b.send("READY")
            assert "MATCH_FOUND bob" in a.until("FLEET_END")
            assert "MATCH_START" in b.until("FLEET_END")
            a.send(fleet)
            a.until("FLEET_READY")
            b.send(fleet)
            a.until("YOUR_TURN")
            b.until("OPPONENT_TURN")
            # The opponent's hidden fleet never appears on the live board.
            a.send("SHOW_OPPONENT")
            board = a.until("BOARD_END")
            assert all(line.endswith("..........") for line in board if line.startswith("BOARD_ROW"))
            a.send("SHOOT A1")
            assert "SHOT_RESULT A1 HIT" in a.until("YOUR_TURN")
            b.until("OPPONENT_TURN")
            a.send("RESIGN")
            result = a.until("GAME_RESULT")[-1].split()
            assert result[1:3] == ["LOSE", "RESIGN"] and result[-2:] == ["984", "-16"]
            match_id = int(result[3])
            assert b.until("GAME_RESULT")[-1].endswith("1016 16")
            a.send("HISTORY 0")
            assert any(f"HISTORY_ITEM {match_id} " in x for x in a.until("HISTORY_END"))
            a.send(f"REPLAY {match_id}")
            replay = a.until("REPLAY_END")
            assert sum(" SHIP " in x for x in replay) == 10
            assert any(" SHOT 0 A1 HIT" in x for x in replay)
            # Reconstruct both initial fleets and the shot from ordered events.
            boards = [[list("." * 10) for _ in range(10)] for _ in range(2)]
            sequences = []
            for line in replay:
                if not line.startswith("REPLAY_EVENT "):
                    continue
                fields = line.split()
                sequences.append(int(fields[1]))
                event = fields[3:]
                if event[0] == "SHIP":
                    player, length = int(event[1]), int(event[5])
                    row, col = ord(event[3][0]) - ord("A"), int(event[3][1:]) - 1
                    for step in range(length):
                        r = row + (step if event[4] == "V" else 0)
                        c = col + (step if event[4] == "H" else 0)
                        assert boards[player][r][c] == "."
                        boards[player][r][c] = "S"
                elif event[0] == "SHOT":
                    defender = 1 - int(event[1])
                    row, col = ord(event[2][0]) - ord("A"), int(event[2][1:]) - 1
                    assert event[3] == ("HIT" if boards[defender][row][col] == "S" else "MISS")
                    boards[defender][row][col] = "X" if event[3] == "HIT" else "O"
            assert sequences == list(range(1, len(sequences) + 1))
            assert boards[1][0][0] == "X"
            assert sum(cell == "S" for row in boards[1] for cell in row) == 16
            outsider.send(f"REPLAY {match_id}")
            assert outsider.line() == "ERROR Replay unavailable"

            # A live disconnect is a ranked loss; cached Elo and SQL agree.
            a.send("READY")
            a.until("READY_OK")
            b.send("READY")
            a.until("FLEET_END")
            b.until("FLEET_END")
            a.close()
            assert "WIN DISCONNECT" in b.until("GAME_RESULT")[-1]
            a = Peer(port)
            peers.append(a)
            a.login("alice", register=False)
            a.send("STATS")
            assert a.line().split()[2:] == ["0", "2", "0"]

            # A second server cannot mark this running server's matches aborted.
            a.send("CHALLENGE bob")
            a.until("CHALLENGE_SENT")
            b.until("CHALLENGE_FROM")
            b.send("ACCEPT_CHALLENGE alice")
            a.until("FLEET_END")
            b.until("FLEET_END")
            other = subprocess.run([binary, str(port_number()), str(db)], capture_output=True, timeout=5)
            assert other.returncode != 0 and b"lock" in other.stderr.lower()
            with sqlite3.connect(db) as conn:
                assert conn.execute("SELECT COUNT(*) FROM matches WHERE status='ACTIVE'").fetchone()[0] == 1
                before = conn.execute("SELECT username,rating FROM users ORDER BY username").fetchall()
                unfinished = conn.execute("SELECT id FROM matches WHERE status='ACTIVE'").fetchone()[0]
            stop(process, crash=True)
            for peer in peers:
                peer.close()
            peers.clear()
            process = start(binary, port, db)
            with sqlite3.connect(db) as conn:
                assert conn.execute("SELECT status,reason FROM matches WHERE id=?", (unfinished,)).fetchone() == ("ABORTED", "SERVER_RESTART")
                assert conn.execute("SELECT username,rating FROM users ORDER BY username").fetchall() == before
            a = Peer(port)
            peers.append(a)
            a.login("alice", register=False)
            a.send(f"REPLAY {unfinished}")
            assert a.line() == "ERROR Replay unavailable"
            a.send("HISTORY 0")
            assert a.until("HISTORY_END")[0] == "HISTORY_BEGIN 2"
            a.send(f"REPLAY {match_id}")
            assert any(" SHOT 0 A1 HIT" in x for x in a.until("REPLAY_END"))
            a.send("QUIT")
            assert a.line() == "BYE"
        finally:
            stop(process)
            for peer in peers:
                peer.close()
    print("PASS: TCP framing, matchmaking, hidden boards, history, disconnects and crash recovery")


if __name__ == "__main__":
    run(str(pathlib.Path(sys.argv[1]).resolve()))
