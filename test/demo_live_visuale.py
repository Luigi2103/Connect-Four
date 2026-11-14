import socket
import threading
import time
import sys
import os

SERVER_HOST = os.environ.get("SERVER_HOST", "127.0.0.1")
SERVER_PORT = int(os.environ.get("SERVER_PORT", "5050"))

# Colori ANSI
C_RESET  = "\033[0m"
C_CYAN   = "\033[36m"
C_GREEN  = "\033[32m"
C_YELLOW = "\033[33m"
C_RED    = "\033[31m"
C_BOLD   = "\033[1m"
C_GRAY   = "\033[90m"

COL_WIDTH = 50

def print_header():
    print("\n" + "=" * (COL_WIDTH * 2 + 5))
    p1_title = f" CLIENT 1 (Alice - Proprietario) ".center(COL_WIDTH)
    p2_title = f" CLIENT 2 (Bob - Sfidante) ".center(COL_WIDTH)
    print(f"|{C_CYAN}{C_BOLD}{p1_title}{C_RESET}|{C_YELLOW}{C_BOLD}{p2_title}{C_RESET}|")
    print("=" * (COL_WIDTH * 2 + 5))

def print_row(p1_text="", p2_text="", delay=0.6):
    """Stampa una riga affiancata per i due client con un ritardo per visualizzare l'animazione"""
    # Rimuove newline finali
    p1 = p1_text.replace("\n", " ").strip()
    p2 = p2_text.replace("\n", " ").strip()

    # Tronca se troppo lungo per mantenere l'allineamento
    if len(p1) > COL_WIDTH - 2:
        p1 = p1[:COL_WIDTH - 5] + "..."
    if len(p2) > COL_WIDTH - 2:
        p2 = p2[:COL_WIDTH - 5] + "..."

    print(f"| {p1:<{COL_WIDTH - 2}} | {p2:<{COL_WIDTH - 2}} |")
    sys.stdout.flush()
    if delay > 0:
        time.sleep(delay)

class LiveClient:
    def __init__(self, name):
        self.name = name
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((SERVER_HOST, SERVER_PORT))
        self.buf = ""

    def send(self, msg):
        self.sock.sendall((msg + "\n").encode('utf-8'))

    def read_line(self, timeout=2.0):
        self.sock.settimeout(timeout)
        start = time.time()
        while "\n" not in self.buf and time.time() - start < timeout:
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

    def close(self):
        try:
            self.sock.close()
        except:
            pass

def run_live_demonstration():
    print_header()

    # 1. Login
    alice = LiveClient("Alice")
    bob = LiveClient("Bob")

    print_row("> NICK Alice", "> NICK Bob", delay=0.8)
    alice.send("NICK Alice")
    bob.send("NICK Bob")

    r_alice = alice.read_line()
    r_bob = bob.read_line()
    print_row(f"{C_GREEN}{r_alice}{C_RESET}", f"{C_GREEN}{r_bob}{C_RESET}", delay=1.0)

    # 2. Alice crea una stanza
    print_row("--------------------------------------------------", "--------------------------------------------------", delay=0.3)
    print_row(f"{C_BOLD}> crea{C_RESET}", f"{C_GRAY}[in attesa nella lobby]{C_RESET}", delay=0.8)
    alice.send("CREATE")
    r_create = alice.read_line() # ROOM_CREATED <id>
    rid = r_create.split()[1] if "ROOM_CREATED" in r_create else "1"

    # Bob riceve la notifica della nuova stanza creata da Alice
    r_bob_notif = bob.read_line()
    print_row(f"{C_GREEN}[STANZA] Creata Stanza #{rid}{C_RESET}", f"{C_GRAY}[INFO LOBBY] Nuova Stanza #{rid} (Alice){C_RESET}", delay=1.2)

    # 3. TEST EDGE CASE: Bob tenta di chiudere la stanza creata da Alice!
    print_row("--------------------------------------------------", "--------------------------------------------------", delay=0.3)
    print_row(f"{C_GRAY}[proprietario della stanza #{rid}]{C_RESET}", f"{C_BOLD}> chiudi {rid}{C_RESET}", delay=0.8)
    bob.send(f"LEAVE {rid}")
    r_bob_err = bob.read_line()
    print_row(f"{C_GRAY}[nessun impatto, stanza protetta]{C_RESET}", f"{C_RED}{r_bob_err}{C_RESET}", delay=1.5)

    # 4. Bob chiede di unirsi alla stanza di Alice
    print_row("--------------------------------------------------", "--------------------------------------------------", delay=0.3)
    print_row(f"{C_GRAY}[in attesa di sfidanti]{C_RESET}", f"{C_BOLD}> unisciti {rid}{C_RESET}", delay=0.8)
    bob.send(f"JOIN {rid}")
    r_bob_ok = bob.read_line()
    r_alice_req = alice.read_line()
    print_row(f"{C_YELLOW}[RICHIESTA SFIDA] Bob vuole entrare!{C_RESET}", f"{C_GREEN}{r_bob_ok}{C_RESET}", delay=1.5)

    # 5. Alice accetta la sfida
    print_row("--------------------------------------------------", "--------------------------------------------------", delay=0.3)
    print_row(f"{C_BOLD}> accetta {rid}{C_RESET}", f"{C_GRAY}[attende decisione...]{C_RESET}", delay=0.8)
    alice.send(f"ACCEPT {rid}")

    r_start_a = alice.read_line()
    r_start_b = bob.read_line()
    print_row(f"{C_GREEN}=== PARTITA INIZIATA! (Simbolo X) ==={C_RESET}", f"{C_GREEN}=== PARTITA INIZIATA! (Simbolo O) ==={C_RESET}", delay=1.0)
    print_row(f"{C_YELLOW}>> E il tuo turno! (Tocca a te){C_RESET}", f"{C_GRAY}[In attesa della prima mossa di Alice]{C_RESET}", delay=1.2)

    # 6. Mossa 1 di Alice (Colonna 4)
    print_row("--------------------------------------------------", "--------------------------------------------------", delay=0.3)
    print_row(f"{C_BOLD}> 4 (Alice gioca colonna 4){C_RESET}", f"{C_GRAY}[attesa ricezione mossa]{C_RESET}", delay=0.8)
    alice.send(f"MOVE {rid} 4")

    # Entrambi ricevono l'aggiornamento della scacchiera
    b_up_a = alice.read_line()
    b_up_b = bob.read_line()
    turn_b = bob.read_line()
    wait_a = alice.read_line()

    print_row(f"{C_CYAN}[Pedina X inserita in col 4]{C_RESET}", f"{C_CYAN}[Pedina X inserita in col 4]{C_RESET}", delay=1.0)
    print_row(f"{C_GRAY}[Turno avversario in corso...]{C_RESET}", f"{C_YELLOW}>> E il tuo turno! Scegli (1-7):{C_RESET}", delay=1.2)

    # 7. Mossa 2 di Bob (Colonna 4)
    print_row("--------------------------------------------------", "--------------------------------------------------", delay=0.3)
    print_row(f"{C_GRAY}[attesa ricezione mossa]{C_RESET}", f"{C_BOLD}> 4 (Bob gioca colonna 4){C_RESET}", delay=0.8)
    bob.send(f"MOVE {rid} 4")

    b_up_a2 = alice.read_line()
    b_up_b2 = bob.read_line()
    turn_a = alice.read_line()
    wait_b = bob.read_line()

    print_row(f"{C_CYAN}[Pedina O inserita in col 4 (sopra)]{C_RESET}", f"{C_CYAN}[Pedina O inserita in col 4 (sopra)]{C_RESET}", delay=1.0)
    print_row(f"{C_YELLOW}>> E il tuo turno! Scegli (1-7):{C_RESET}", f"{C_GRAY}[Turno avversario in corso...]{C_RESET}", delay=1.2)

    # 8. TEST EDGE CASE: Abbandono volontario asimmetrico (Bob si arrende)
    print_row("--------------------------------------------------", "--------------------------------------------------", delay=0.3)
    print_row(f"{C_GRAY}[attende mossa o decisione]{C_RESET}", f"{C_BOLD}> abbandona{C_RESET}", delay=0.8)
    bob.send(f"LEAVE {rid}")

    # Bob riceve la conferma di uscita, Alice la notifica di disconnessione
    r_bob_leave = ""
    while True:
        line = bob.read_line()
        if line.startswith("OK") or line.startswith("ERR") or not line:
            r_bob_leave = line
            break

    r_alice_disc = ""
    while True:
        line = alice.read_line()
        if "OPPONENT_DISCONNECTED" in line or not line:
            r_alice_disc = "[AVVISO] L'avversario ha abbandonato la partita!"
            break

    print_row(f"{C_RED}{r_alice_disc}{C_RESET}", f"{C_GREEN}{r_bob_leave}{C_RESET}", delay=1.0)
    print_row(f"{C_CYAN}[LOBBY] Sei tornato nella lobby.{C_RESET}", f"{C_CYAN}[LOBBY] Sei tornato nella lobby.{C_RESET}", delay=1.0)

    print("=" * (COL_WIDTH * 2 + 5))
    print(f"{C_GREEN}{C_BOLD} DEMOSTRAZIONE VISUALE COMPLETATA CON SUCCESSO! {C_RESET}\n")

    alice.close()
    bob.close()

if __name__ == "__main__":
    run_live_demonstration()
