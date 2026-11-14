import socket
import threading
import subprocess
import time
import sys
import os

SERVER_HOST = os.environ.get("SERVER_HOST", "127.0.0.1")
SERVER_PORT = int(os.environ.get("SERVER_PORT", "5050"))

COLOR_RESET = "\033[0m"
COLOR_YELLOW = "\033[93m"
COLOR_RED = "\033[91m"
COLOR_CYAN = "\033[96m"
COLOR_GREEN = "\033[92m"
COLOR_BOLD = "\033[1m"
COLOR_GRAY = "\033[90m"

if os.name == 'nt':
    os.system('')

SCENARIOS = {
    "orizzontale": {
        "title": "Vittoria Orizzontale (Riga inferiore)",
        "moves": [1, 1, 2, 2, 3, 3, 4],
        "expected": "WIN",
        "description": "P1 allinea 4 pedine 'X' sulla prima riga libera (colonne 1, 2, 3, 4)"
    },
    "verticale": {
        "title": "Vittoria Verticale (Colonna 1)",
        "moves": [1, 2, 1, 2, 1, 2, 1],
        "expected": "WIN",
        "description": "P1 impila 4 pedine 'X' consecutivamente nella colonna 1"
    },
    "diag_down": {
        "title": "Vittoria Diagonale Discendente (\\)",
        "moves": [4, 3, 3, 2, 1, 1, 2, 1, 2, 5, 1],
        "expected": "WIN",
        "description": "P1 allinea 4 pedine 'X' dall'alto-sx al basso-dx (colonne 4, 3, 2, 1)"
    },
    "diag_up": {
        "title": "Vittoria Diagonale Ascendente (/)",
        "moves": [1, 2, 2, 3, 4, 3, 3, 4, 4, 5, 4],
        "expected": "WIN",
        "description": "P1 allinea 4 pedine 'X' dal basso-sx all'alto-dx (colonne 1, 2, 3, 4)"
    },
    "pareggio": {
        "title": "Partita Patta / Pareggio (42 mosse)",
        "moves": [1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 5, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7],
        "expected": "DRAW",
        "description": "Scacchiera interamente riempita (42 pedine) senza alcun allineamento vincente"
    }
}

def render_board(board_str):
    if len(board_str) != 42:
        return
    print("\n    1   2   3   4   5   6   7")
    print("  ┌───┬───┬───┬───┬───┬───┬───┐")
    for r in range(6):
        row_str = "  │"
        for c in range(7):
            ch = board_str[r * 7 + c]
            if ch == 'X':
                cell = f" {COLOR_YELLOW}{COLOR_BOLD}X{COLOR_RESET} "
            elif ch == 'O':
                cell = f" {COLOR_RED}{COLOR_BOLD}O{COLOR_RESET} "
            else:
                cell = "   "
            row_str += cell + "│"
        print(row_str)
        if r < 5:
            print("  ├───┼───┼───┼───┼───┼───┼───┤")
    print("  └───┴───┴───┴───┴───┴───┴───┘")

def run_simulated_player(role, scenario_key, move_delay=0.7):
    scen = SCENARIOS[scenario_key]
    moves = scen["moves"]

    nick = "Player_1 (Giallo)" if role == "p1" else "Player_2 (Rosso)"
    my_turn_modulo = 0 if role == "p1" else 1

    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((SERVER_HOST, SERVER_PORT))

    def send(msg):
        s.sendall((msg.strip() + "\n").encode('utf-8'))

    print(f"{COLOR_CYAN}{COLOR_BOLD}=== {nick.upper()} - SIMULATORE PARTITA ==={COLOR_RESET}")
    print(f"Scenario attivo: {COLOR_BOLD}{scen['title']}{COLOR_RESET}")
    print(f"Descrizione: {scen['description']}\n")

    send(f"NICK {nick.split()[0]}")

    room_id = -1
    current_move_idx = 0
    buffer = ""
    joined = False
    in_game = False

    def p2_poller():
        time.sleep(1.0)
        while not in_game and not joined:
            try:
                send("LIST")
            except Exception:
                break
            time.sleep(1.5)

    if role == "p2":
        t_poll = threading.Thread(target=p2_poller, daemon=True)
        t_poll.start()

    if role == "p1":
        time.sleep(0.5)
        send("CREATE")

    while True:
        try:
            chunk = s.recv(1024).decode('utf-8', errors='ignore')
            if not chunk:
                break
            buffer += chunk
            while "\n" in buffer:
                line, buffer = buffer.split("\n", 1)
                line = line.strip()
                if not line:
                    continue

                tokens = line.split()
                cmd = tokens[0]

                if cmd == "ROOM_CREATED":
                    room_id = int(tokens[1])
                    print(f"{COLOR_GREEN}[STANZA]{COLOR_RESET} Creata stanza #{room_id}. Attendo avversario...")

                elif cmd in ("ROOM_NOTIF", "LOBBY_ROOM") and role == "p2" and not joined:
                    if len(tokens) >= 3:
                        rid = int(tokens[1])
                        st = tokens[2]
                        if st == "IN_ATTESA":
                            joined = True
                            time.sleep(0.3)
                            print(f"{COLOR_CYAN}[JOIN]{COLOR_RESET} Trovata stanza #{rid} in attesa. Richiedo ingresso...")
                            send(f"JOIN {rid}")

                elif cmd == "JOIN_REQUEST" and role == "p1":
                    rid = int(tokens[1])
                    applicant = tokens[2]
                    print(f"{COLOR_YELLOW}[RICHIESTA]{COLOR_RESET} {applicant} vuole giocare. Accetto...")
                    time.sleep(0.4)
                    send(f"ACCEPT {rid}")

                elif cmd == "ERR" and role == "p2" and joined and not in_game:
                    joined = False

                elif cmd == "GAME_START":
                    in_game = True
                    room_id = int(tokens[1])
                    print(f"\n{COLOR_GREEN}{COLOR_BOLD}*** PARTITA AVVIATA! ***{COLOR_RESET}")
                    if role == "p1":
                        time.sleep(move_delay)
                        if current_move_idx < len(moves):
                            col = moves[current_move_idx]
                            print(f"{COLOR_YELLOW}>> P1 gioca nella colonna {col}{COLOR_RESET}")
                            send(f"MOVE {room_id} {col}")

                elif cmd == "BOARD_UPDATE":
                    col = tokens[1]
                    sym = tokens[3]
                    b_str = tokens[4]
                    render_board(b_str)
                    current_move_idx += 1

                elif cmd == "YOUR_TURN":
                    if current_move_idx < len(moves):
                        time.sleep(move_delay)
                        col = moves[current_move_idx]
                        who = "P1" if role == "p1" else "P2"
                        col_color = COLOR_YELLOW if role == "p1" else COLOR_RED
                        print(f"{col_color}>> {who} gioca nella colonna {col}{COLOR_RESET}")
                        send(f"MOVE {room_id} {col}")

                elif cmd == "GAME_OVER":
                    outcome = tokens[1]
                    msg = " ".join(tokens[2:])
                    if outcome == "WIN":
                        print(f"\n{COLOR_GREEN}{COLOR_BOLD}🏆 RISULTATO: {msg}{COLOR_RESET}")
                    elif outcome == "LOSE":
                        print(f"\n{COLOR_RED}{COLOR_BOLD}❌ RISULTATO: {msg}{COLOR_RESET}")
                        print(f"{COLOR_GRAY}(Il giocatore sconfitto lascia la partita e torna in lobby come da specifica){COLOR_RESET}")
                        print(f"\n{COLOR_GREEN}--- Simulazione completata con successo! Premi INVIO per chiudere. ---{COLOR_RESET}")
                        input()
                        return
                    else:
                        print(f"\n{COLOR_YELLOW}{COLOR_BOLD}⚖️ RISULTATO: {msg}{COLOR_RESET}")

                elif cmd == "REMATCH_OFFER":
                    offer_msg = line[14:]
                    print(f"\n{COLOR_CYAN}{COLOR_BOLD}[OFFERTA DAL SERVER]{COLOR_RESET} {offer_msg}")

                    if scenario_key == "pareggio":
                        if role == "p1":
                            print(f"\n{COLOR_YELLOW}{COLOR_BOLD}--- TEST AUTOMATICO GESTIONE RIVINCITA ---{COLOR_RESET}")
                            time.sleep(1.0)
                            print(f"{COLOR_YELLOW}[TEST 1 - SINTASSI ERRATA] P1 invia: REMATCH {room_id} ERRORE{COLOR_RESET}")
                            send(f"REMATCH {room_id} ERRORE")
                        elif role == "p2":
                            print(f"\n{COLOR_YELLOW}{COLOR_BOLD}--- TEST AUTOMATICO GESTIONE RIVINCITA ---{COLOR_RESET}")
                            time.sleep(3.2) # Attende che P1 testi l'errore e voti YES
                            print(f"{COLOR_YELLOW}[TEST 3 - RIFIUTO RIVINCITA] P2 invia: REMATCH {room_id} NO{COLOR_RESET}")
                            send(f"REMATCH {room_id} NO")
                    else:
                        print(f"\n{COLOR_GREEN}--- Simulazione completata con successo! Premi INVIO per chiudere. ---{COLOR_RESET}")
                        input()
                        return

                elif cmd == "ERR" and "rematch" in line.lower():
                    print(f"{COLOR_RED}[SERVER - ERRORE CATTURATO] {line}{COLOR_RESET}")
                    time.sleep(1.0)
                    print(f"{COLOR_YELLOW}[TEST 2 - VOTO FAVOREVOLE] P1 invia: REMATCH {room_id} YES{COLOR_RESET}")
                    send(f"REMATCH {room_id} YES")

                elif cmd == "OK" and any(k in line.lower() for k in ("voto", "conclusa", "rifiutato", "lobby")):
                    print(f"{COLOR_GREEN}[SERVER] {line}{COLOR_RESET}")
                    if any(k in line.lower() for k in ("conclusa", "rifiutato")):
                        print(f"\n{COLOR_GREEN}--- Simulazione e test rivincita completati! Premi INVIO per chiudere. ---{COLOR_RESET}")
                        input()
                        return

        except Exception as e:
            print(f"Errore client: {e}")
            break

def run_server_process():
    print(f"{COLOR_CYAN}{COLOR_BOLD}=== SERVER FORZA 4 (Log e Monitoraggio Connessioni) ==={COLOR_RESET}")
    try:
        res = subprocess.run(["docker", "ps", "-q", "-f", "name=connect4_server"], capture_output=True, text=True)
        if res.stdout.strip():
            print(f"{COLOR_GREEN}[INFO] Server attivo nel container Docker (connect4_server).{COLOR_RESET}")
            print(f"{COLOR_GRAY}Streaming log in tempo reale (Premi CTRL+C per uscire)...{COLOR_RESET}\n")
            os.system("docker logs -f --tail 15 connect4_server")
            return
    except Exception:
        pass

    print(f"{COLOR_GREEN}[INFO] Avvio server in ascolto sulla porta {SERVER_PORT}...{COLOR_RESET}\n")
    cmd = 'wsl bash -c "cd server && gcc -Wall -Wextra -pthread -Iinclude -std=c99 src/*.c -o server_forza4 && ./server_forza4 5050"'
    os.system(cmd)

def open_terminal(title, args_str):
    base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    py_exec = sys.executable

    if os.name == 'nt':
        # Su Windows usa apici singoli su WorkingDirectory per supportare spazi nei percorsi
        ps_cmd = (
            f"Start-Process powershell -WorkingDirectory '{base_dir}' "
            f"-ArgumentList '-NoExit', '-Command', '& \"{py_exec}\" test/simulate_game.py {args_str}'"
        )
        subprocess.Popen(["powershell", "-Command", ps_cmd])
    elif sys.platform == 'darwin':
        # Supporto macOS
        osa_script = f'tell application "Terminal" to do script "cd \\"{base_dir}\\" && \\"{py_exec}\\" test/simulate_game.py {args_str}"'
        subprocess.Popen(["osascript", "-e", osa_script])
    else:
        # Supporto Linux (gnome-terminal o xterm)
        cmd = f"cd '{base_dir}' && '{py_exec}' test/simulate_game.py {args_str}; exec bash"
        try:
            subprocess.Popen(["gnome-terminal", "--", "bash", "-c", cmd])
        except Exception:
            subprocess.Popen(["xterm", "-e", f"bash -c \"{cmd}\""])

def launch_three_terminals(scenario_key):
    scen = SCENARIOS.get(scenario_key, SCENARIOS["orizzontale"])
    print(f"\n{COLOR_GREEN}{COLOR_BOLD}Avvio di 3 terminali a schermo per la simulazione:{COLOR_RESET}")
    print(f"-> Scenario: {COLOR_BOLD}{scen['title']}{COLOR_RESET}")
    print(f"-> Dettagli: {scen['description']}\n")

    # 1. Avvia Server
    open_terminal("SERVER - Forza 4", "--role server")
    time.sleep(1.5)

    # 2. Avvia Player 1
    open_terminal("GIOCATORE 1 (Giallo)", f"--role p1 --scenario {scenario_key}")
    time.sleep(0.5)

    # 3. Avvia Player 2
    open_terminal("GIOCATORE 2 (Rosso)", f"--role p2 --scenario {scenario_key}")

class BufferedSocket:
    def __init__(self, sock):
        self.sock = sock
        self.buf = ""

    def send(self, msg):
        self.sock.sendall((msg.strip() + "\n").encode('utf-8'))

    def read_line(self, timeout=3.0):
        self.sock.settimeout(timeout)
        start = time.time()
        while time.time() - start < timeout:
            if "\n" in self.buf:
                line, self.buf = self.buf.split("\n", 1)
                return line.strip()
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

    def close(self):
        try:
            self.sock.close()
        except:
            pass

def test_single_scenario_visual(scen_key):
    scen = SCENARIOS[scen_key]
    moves = scen["moves"]

    raw1 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    raw2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    raw1.connect((SERVER_HOST, SERVER_PORT))
    raw2.connect((SERVER_HOST, SERVER_PORT))

    p1 = BufferedSocket(raw1)
    p2 = BufferedSocket(raw2)

    p1.send("NICK Test_P1")
    p2.send("NICK Test_P2")
    p1.read_until("OK")
    p2.read_until("OK")

    p1.send("CREATE")
    resp = p1.read_until("ROOM_CREATED")
    rid = int(resp.split()[1])

    p2.send(f"JOIN {rid}")
    p2.read_until("OK")

    p1.read_until("JOIN_REQUEST")
    p1.send(f"ACCEPT {rid}")

    p1.read_until("GAME_START")
    p2.read_until("GAME_START")

    final_outcome = None
    last_board_str = ""
    for i, col in enumerate(moves):
        active = p1 if (i % 2 == 0) else p2
        passive = p2 if (i % 2 == 0) else p1

        active.send(f"MOVE {rid} {col}")
        up1 = active.read_until("BOARD_UPDATE")
        up2 = passive.read_until("BOARD_UPDATE")

        if len(up1.split()) >= 5:
            last_board_str = up1.split()[4]

        l1 = active.read_until(["GAME_OVER", "WAIT_TURN", "YOUR_TURN"])
        l2 = passive.read_until(["GAME_OVER", "WAIT_TURN", "YOUR_TURN"])

        if l1.startswith("GAME_OVER"):
            final_outcome = l1.split()[1]
            break
        elif l2.startswith("GAME_OVER"):
            final_outcome = l2.split()[1]
            break

    # Stampa visiva della scacchiera finale
    if last_board_str:
        render_board(last_board_str)

    # In caso di pareggio, esegui e mostra la gestione della rivincita (errore, si, no)
    if scen_key == "pareggio" and final_outcome == "DRAW":
        p1.read_until("REMATCH_OFFER")
        p2.read_until("REMATCH_OFFER")

        print(f"\n{COLOR_CYAN}{COLOR_BOLD}--- DIMOSTRAZIONE PROTOCOLLO RIVINCITA ---{COLOR_RESET}")

        # 1. Test scelta non valida
        p1.send(f"REMATCH {rid} ERRORE")
        err_resp = p1.read_until(["ERR", "OK"])
        print(f"  {COLOR_YELLOW}[1. TEST SINTASSI ERRATA]{COLOR_RESET} P1 invia: 'REMATCH {rid} ERRORE'")
        print(f"     -> Risposta Server: {COLOR_RED}{err_resp}{COLOR_RESET} (Errore catturato!)")

        # 2. Test voto favorevole (SI)
        p1.send(f"REMATCH {rid} YES")
        ok_p1 = p1.read_until(["ERR", "OK"])
        print(f"  {COLOR_YELLOW}[2. TEST VOTO FAVOREVOLE]{COLOR_RESET} P1 invia: 'REMATCH {rid} YES'")
        print(f"     -> Risposta Server: {COLOR_GREEN}{ok_p1}{COLOR_RESET}")

        # 3. Test rifiuto (NO) da parte del secondo giocatore
        p2.send(f"REMATCH {rid} NO")
        ok_p2 = p2.read_until(["ERR", "OK"])
        notif_p1 = p1.read_until(["ERR", "OK"])
        print(f"  {COLOR_YELLOW}[3. TEST RIFIUTO RIVINCITA]{COLOR_RESET} P2 invia: 'REMATCH {rid} NO'")
        print(f"     -> Risposta a P2: {COLOR_GREEN}{ok_p2}{COLOR_RESET}")
        print(f"     -> Notifica a P1: {COLOR_CYAN}{notif_p1}{COLOR_RESET}")
        print(f"     -> {COLOR_GREEN}Stanza deallocata e sessione chiusa regolarmente.{COLOR_RESET}")

    p1.close()
    p2.close()
    return final_outcome

def run_test_all():
    print("=" * 65)
    print("   TEST AUTOMATIZZATO DI TUTTE LE CASISTICHE DI GIOCO   ")
    print("=" * 65)
    
    results = {}
    for key, info in SCENARIOS.items():
        print(f"\n{COLOR_CYAN}================================================================={COLOR_RESET}")
        print(f"Esecuzione test: {COLOR_BOLD}{info['title']}{COLOR_RESET}")
        print(f"{COLOR_GRAY}Descrizione: {info['description']}{COLOR_RESET}")
        outcome = test_single_scenario_visual(key)
        expected = info["expected"]
        passed = (outcome == expected)
        results[key] = (passed, outcome, expected)
        
        status_str = f"{COLOR_GREEN}SUPERATO (Esito: {outcome}){COLOR_RESET}" if passed else f"{COLOR_RED}FALLITO (Ottenuto: {outcome}, Atteso: {expected}){COLOR_RESET}"
        print(f"-> Esito verifica: {status_str}")

    print("\n" + "=" * 65)
    print("RIEPILOGO FINALE DEI TEST:")
    all_ok = True
    for key, (passed, got, exp) in results.items():
        mark = "✓" if passed else "✗"
        col = COLOR_GREEN if passed else COLOR_RED
        print(f" {col}[{mark}] {SCENARIOS[key]['title']:<38} -> {got}{COLOR_RESET}")
        if not passed: all_ok = False
    print("=" * 65)
    
    if all_ok:
        print(f"{COLOR_GREEN}{COLOR_BOLD}TUTTE LE 4 CASISTICHE DI VITTORIA E IL PAREGGIO FUNZIONANO AL 100%!{COLOR_RESET}\n")
    else:
        print(f"{COLOR_RED}Alcuni scenari hanno dato esito inatteso.{COLOR_RESET}\n")

if __name__ == "__main__":
    if "--role" in sys.argv:
        role_idx = sys.argv.index("--role") + 1
        role = sys.argv[role_idx]
        if role == "server":
            run_server_process()
        else:
            scen_key = "orizzontale"
            if "--scenario" in sys.argv:
                s_idx = sys.argv.index("--scenario") + 1
                scen_key = sys.argv[s_idx]
            run_simulated_player(role, scen_key)
        sys.exit(0)

    if len(sys.argv) >= 2 and sys.argv[1] in ["--test-all", "tutti", "test"]:
        run_test_all()
        sys.exit(0)

    # Se passato un nome di scenario da riga di comando
    if len(sys.argv) >= 2 and sys.argv[1].lower() in SCENARIOS:
        launch_three_terminals(sys.argv[1].lower())
        sys.exit(0)

    # Menu interattivo con selezione e opzione predefinita
    print(f"\n{COLOR_CYAN}{COLOR_BOLD}============================================================{COLOR_RESET}")
    print(f"{COLOR_CYAN}{COLOR_BOLD}       SIMULATORE PARTITA A FORZA 4 - 3 TERMINALI          {COLOR_RESET}")
    print(f"{COLOR_CYAN}{COLOR_BOLD}============================================================{COLOR_RESET}")
    print("Seleziona quale casistica visualizzare a schermo sui 3 terminali:\n")
    print("  1) Vittoria Orizzontale         (P1 vince su riga)")
    print("  2) Vittoria Verticale           (P1 vince su colonna 1)")
    print("  3) Diagonale Discendente (\\)    (P1 vince da alto-sx a basso-dx)")
    print("  4) Diagonale Ascendente (/)     (P1 vince da basso-sx ad alto-dx)")
    print("  5) Partita Patta / Pareggio     (42 mosse, scacchiera piena)")
    print("  6) Esegui test headless di TUTTE le casistiche (senza terminali esterni)\n")

    choice = input("Scegli un'opzione [1-6] (default: 1): ").strip()
    mapping = {
        "1": "orizzontale",
        "2": "verticale",
        "3": "diag_down",
        "4": "diag_up",
        "5": "pareggio"
    }

    if choice == "6":
        run_test_all()
    else:
        scen_selected = mapping.get(choice, "orizzontale")
        launch_three_terminals(scen_selected)
