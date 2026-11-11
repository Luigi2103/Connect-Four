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
            line = self.read_line(timeout=0.5)
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
    ok = cli.read_until("OK", timeout=3.0)
    assert ok.startswith("OK"), f"Login fallito per {nick}: {ok}"
    return cli

def test_concurrent_room_creation():
    print("\n[TEST 1] Creazione concorrente di stanze (Verifica unicità ID e integrità lock)...")
    num_clients = 15
    clients = []
    created_ids = []
    barrier = threading.Barrier(num_clients)
    lock = threading.Lock()

    def worker(i):
        try:
            cli = connect_client(f"Creator_{i}")
            with lock:
                clients.append(cli)
            barrier.wait() # Trigger simultaneo di CREATE
            cli.send("CREATE")
            resp = cli.read_until("ROOM_CREATED", timeout=3.0)
            if resp.startswith("ROOM_CREATED"):
                rid = int(resp.split()[1])
                with lock:
                    created_ids.append(rid)
        except Exception as e:
            print(f"  [Creator_{i}] Errore: {e}")

    threads = [threading.Thread(target=worker, args=(i,)) for i in range(num_clients)]
    for t in threads: t.start()
    for t in threads: t.join()

    print(f"  Stanze create con successo: {len(created_ids)}/{num_clients}")
    print(f"  ID registrati: {sorted(created_ids)}")
    
    assert len(created_ids) == num_clients, f"Attese {num_clients} stanze, create {len(created_ids)}"
    assert len(created_ids) == len(set(created_ids)), "ERRORE: Rilevati ID duplicati!"
    print("  -> TEST 1 SUPERATO: Nessun conflitto o duplicato su creazione concorrente.")

    for cli in clients:
        cli.close()

def test_concurrent_join_race():
    print("\n[TEST 2] Race Condition: Join simultaneo di 10 client sulla stessa stanza...")
    owner = connect_client("RoomOwner")
    owner.send("CREATE")
    resp = owner.read_until("ROOM_CREATED")
    room_id = int(resp.split()[1])
    print(f"  Stanza creata: ID #{room_id}")

    num_joiners = 10
    joiners = []
    barrier = threading.Barrier(num_joiners)
    results = []
    lock = threading.Lock()

    def joiner_worker(i):
        try:
            cli = connect_client(f"Joiner_{i}")
            with lock:
                joiners.append(cli)
            barrier.wait() # Invio simultaneo di JOIN
            cli.send(f"JOIN {room_id}")
            line = cli.read_until(["OK", "ERR"], timeout=3.0)
            with lock:
                results.append(line)
        except Exception as e:
            print(f"  [Joiner_{i}] Errore: {e}")

    threads = [threading.Thread(target=joiner_worker, args=(i,)) for i in range(num_joiners)]
    for t in threads: t.start()
    for t in threads: t.join()

    ok_count = sum(1 for r in results if r.startswith("OK"))
    err_count = sum(1 for r in results if r.startswith("ERR"))
    print(f"  Esiti join: {ok_count} accettato/i come pending, {err_count} respinti")
    
    assert ok_count == 1, f"ERRORE: Atteso esattamente 1 pending join, ottenuti {ok_count}!"
    print("  -> TEST 2 SUPERATO: Richieste di join serializzate correttamente senza race condition.")

    owner.close()
    for j in joiners:
        j.close()

def test_concurrent_moves_turn_enforcement():
    print("\n[TEST 3] Race Condition: Invio simultaneo di mosse fuori turno / contemporanee...")
    p1 = connect_client("Player1")
    p2 = connect_client("Player2")

    p1.send("CREATE")
    resp = p1.read_until("ROOM_CREATED")
    rid = int(resp.split()[1])

    p2.send(f"JOIN {rid}")
    p2.read_until("OK")

    # P1 riceve richiesta e accetta
    p1.read_until("JOIN_REQUEST")
    p1.send(f"ACCEPT {rid}")

    # Entrambi ricevono GAME_START
    p1.read_until("GAME_START")
    p2.read_until("GAME_START")

    # P1 inizia per primo
    barrier = threading.Barrier(2)
    p1_res = []
    p2_res = []

    def p1_move():
        barrier.wait()
        p1.send(f"MOVE {rid} 1")
        resp = p1.read_until(["BOARD_UPDATE", "ERR"], timeout=3.0)
        p1_res.append(resp)

    def p2_move():
        barrier.wait()
        p2.send(f"MOVE {rid} 2")
        resp = p2.read_until(["BOARD_UPDATE", "ERR"], timeout=3.0)
        p2_res.append(resp)

    t1 = threading.Thread(target=p1_move)
    t2 = threading.Thread(target=p2_move)
    t1.start(); t2.start()
    t1.join(); t2.join()

    print(f"  Risposta P1 (turno valido): {p1_res}")
    print(f"  Risposta P2 (turno non valido): {p2_res}")

    assert any(r.startswith("BOARD_UPDATE") for r in p1_res), "P1 non ha completato la mossa"
    assert any("ERR" in r or "Non e il tuo turno" in r for r in p2_res), "P2 doveva essere respinto con errore di turno"
    print("  -> TEST 3 SUPERATO: Alternanza atomica dei turni garantita.")

    p1.close()
    p2.close()

def test_disconnect_stress():
    print("\n[TEST 4] Stress test disconnessioni massive improvvise...")
    num_spammers = 25
    barrier = threading.Barrier(num_spammers)

    def spammer(i):
        try:
            cli = connect_client(f"Spam_{i}")
            barrier.wait()
            cli.send("CREATE")
            cli.send("LIST")
            cli.send("QUIT")
            cli.close()
        except:
            pass

    threads = [threading.Thread(target=spammer, args=(i,)) for i in range(num_spammers)]
    for t in threads: t.start()
    for t in threads: t.join()

    time.sleep(0.5)

    # Verifica salute server
    checker = connect_client("HealthChecker")
    checker.send("LIST")
    checker.send("QUIT")
    checker.close()
    print("  -> TEST 4 SUPERATO: Il server è stabile dopo scariche di disconnessioni contemporanee.")

if __name__ == "__main__":
    print("=" * 60)
    print("   SUITE DI TEST CONCORRENZA E RACE CONDITION - FORZA 4   ")
    print("=" * 60)

    try:
        test_concurrent_room_creation()
        test_concurrent_join_race()
        test_concurrent_moves_turn_enforcement()
        test_disconnect_stress()
        print("\n" + "=" * 60)
        print("   TUTTI I TEST DI CONCORRENZA HANNO AVUTO ESITO POSITIVO!   ")
        print("=" * 60)
    except AssertionError as ae:
        print(f"\n[FALLIMENTO TEST]: {ae}")
        sys.exit(1)
    except Exception as e:
        print(f"\n[ERRORE INASPETTATO]: {e}")
        sys.exit(1)
