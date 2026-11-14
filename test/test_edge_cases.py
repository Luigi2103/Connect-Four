import socket
import threading
import time
import sys
import os

SERVER_HOST = os.environ.get("SERVER_HOST", "127.0.0.1")
SERVER_PORT = int(os.environ.get("SERVER_PORT", "5050"))

class TestClient:
    def __init__(self, sock):
        self.sock = sock
        self.buf = ""

    def read_line(self, timeout=3.0):
        self.sock.settimeout(timeout)
        while "\n" not in self.buf:
            try:
                chunk = self.sock.recv(1024).decode('utf-8', errors='ignore')
                if not chunk:
                    break
                self.buf += chunk
            except socket.timeout:
                break
        if "\n" in self.buf:
            line, self.buf = self.buf.split("\n", 1)
            return line.strip()
        line = self.buf.strip()
        self.buf = ""
        return line

    def read_until(self, prefixes, timeout=3.0):
        if isinstance(prefixes, str):
            prefixes = [prefixes]
        start = time.time()
        while time.time() - start < timeout:
            line = self.read_line(timeout=0.3)
            if any(line.startswith(p) for p in prefixes):
                return line
        return ""

    def send(self, msg):
        self.sock.sendall((msg.strip() + "\n").encode('utf-8'))

    def close(self):
        try:
            self.sock.close()
        except:
            pass

def connect_client(nick):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((SERVER_HOST, SERVER_PORT))
    cli = TestClient(s)
    cli.send(f"NICK {nick}")
    ok = cli.read_until(["OK", "ERR"], timeout=3.0)
    return cli, ok

def test_unauthorized_room_close():
    print("\n[TEST EDGE 1] Verifica autorizzazione chiusura stanza (anti-tampering)...")
    cli_a, ok_a = connect_client("Alice")
    assert ok_a.startswith("OK"), f"Login Alice fallito: {ok_a}"
    cli_a.send("CREATE")
    resp_create = cli_a.read_until("ROOM_CREATED")
    rid = int(resp_create.split()[1])

    cli_b, ok_b = connect_client("Bob")
    assert ok_b.startswith("OK"), f"Login Bob fallito: {ok_b}"

    # Bob tenta di chiudere la stanza di Alice
    cli_b.send(f"LEAVE {rid}")
    resp_leave = cli_b.read_until(["OK", "ERR"])
    print(f"  Risposta a Bob (non autorizzato): {resp_leave}")
    assert "ERR Non puoi chiudere la stanza se non sei il proprietario" in resp_leave, f"Bob non doveva poter chiudere: {resp_leave}"

    # Alice chiude regolarmente la propria stanza
    cli_a.send(f"LEAVE {rid}")
    resp_close = cli_a.read_until(["OK", "ERR"])
    print(f"  Risposta ad Alice (proprietario): {resp_close}")
    assert resp_close.startswith("OK"), f"Alice doveva poter chiudere: {resp_close}"

    cli_a.close()
    cli_b.close()
    print("  -> SUPERATO: Controllo di autorizzazione stanza integro.")

def test_reject_join_feedback():
    print("\n[TEST EDGE 2] Notifica di rifiuto richiesta di sfida...")
    owner, _ = connect_client("OwnerTest")
    owner.send("CREATE")
    rid = int(owner.read_until("ROOM_CREATED").split()[1])

    applicant, _ = connect_client("ApplicantTest")
    applicant.send(f"JOIN {rid}")
    applicant.read_until("OK")

    owner.read_until("JOIN_REQUEST")
    owner.send(f"REJECT {rid}")

    owner_resp = owner.read_until(["OK", "ERR"])
    applicant_resp = applicant.read_until("JOIN_REJECTED")

    print(f"  Risposta al creatore: {owner_resp}")
    print(f"  Risposta allo sfidante: {applicant_resp}")

    assert owner_resp.startswith("OK"), f"Owner doveva ricevere OK: {owner_resp}"
    assert applicant_resp.startswith("JOIN_REJECTED"), f"Applicant doveva ricevere JOIN_REJECTED: {applicant_resp}"

    owner.close()
    applicant.close()
    print("  -> SUPERATO: Feedback bidirezionale su rifiuto verificato.")

def test_voluntary_leave_in_game():
    print("\n[TEST EDGE 3] Abbandono volontario in partita...")
    p1, _ = connect_client("PlayerA")
    p2, _ = connect_client("PlayerB")

    p1.send("CREATE")
    rid = int(p1.read_until("ROOM_CREATED").split()[1])

    p2.send(f"JOIN {rid}")
    p2.read_until("OK")

    p1.read_until("JOIN_REQUEST")
    p1.send(f"ACCEPT {rid}")

    p1.read_until("GAME_START")
    p2.read_until("GAME_START")

    # PlayerA abbandona
    p1.send(f"LEAVE {rid}")
    p1_resp = p1.read_until(["OK", "ERR", "OPPONENT_DISCONNECTED"])
    p2_resp = p2.read_until("OPPONENT_DISCONNECTED")

    print(f"  Risposta a PlayerA (abbandonante): {p1_resp}")
    print(f"  Risposta a PlayerB (avversario): {p2_resp}")

    assert p1_resp.startswith("OK"), f"PlayerA doveva ricevere conferma OK: {p1_resp}"
    assert p2_resp.startswith("OPPONENT_DISCONNECTED"), f"PlayerB doveva ricevere notifica disconnessione avversario: {p2_resp}"

    p1.close()
    p2.close()
    print("  -> SUPERATO: Abbandono volontario gestito asimmetricamente con successo.")

def test_case_insensitive_nicknames():
    print("\n[TEST EDGE 4] Nickname univoci e case-insensitive...")
    c1, ok1 = connect_client("LuigiV")
    assert ok1.startswith("OK")

    c2, err2 = connect_client("lUiGiV")
    print(f"  Tentativo con 'lUiGiV': {err2}")
    assert "ERR Nickname gia in uso" in err2, f"Atteso errore duplicato case-insensitive: {err2}"

    c1.close()
    c2.close()
    print("  -> SUPERATO: Nickname univoco garantito indipendentemente dal maiuscolo/minuscolo.")

if __name__ == "__main__":
    print("=" * 60)
    print("      SUITE DI TEST CASI LIMITE ED EDGE CASES - FORZA 4     ")
    print("=" * 60)
    try:
        test_unauthorized_room_close()
        test_reject_join_feedback()
        test_voluntary_leave_in_game()
        test_case_insensitive_nicknames()
        print("\n" + "=" * 60)
        print("   TUTTI I TEST SUI CASI LIMITE HANNO AVUTO ESITO POSITIVO!   ")
        print("=" * 60)
    except AssertionError as ae:
        print(f"\n[FALLIMENTO TEST]: {ae}")
        sys.exit(1)
    except Exception as e:
        print(f"\n[ERRORE INASPETTATO]: {e}")
        sys.exit(1)
