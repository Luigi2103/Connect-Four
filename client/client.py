import socket
import threading
import sys
import os
import time

# Abilita sequenze ANSI su terminale Windows
if os.name == 'nt':
    os.system('')

# Costanti colori ANSI
COLOR_RESET = "\033[0m"
COLOR_YELLOW = "\033[93m"
COLOR_RED = "\033[91m"
COLOR_CYAN = "\033[96m"
COLOR_GREEN = "\033[92m"
COLOR_GRAY = "\033[90m"
COLOR_BOLD = "\033[1m"

ROWS = 6
COLS = 7

class ConnectFourClient:
    def __init__(self, host, port):
        self.host = host
        self.port = port
        self.sock = None
        self.running = True

        self.username = ""
        self.current_room_id = -1
        self.my_symbol = None
        self.opponent_name = ""
        self.is_my_turn = False
        self.state = "LOGIN"  # LOGIN, LOBBY, WAITING_APPROVAL, IN_GAME, GAME_OVER

        self.board = [[' ' for _ in range(COLS)] for _ in range(ROWS)]
        self.lock = threading.Lock() #mutex in python
        self.auth_event = threading.Event()
        self.authenticated = False

    def connect(self):
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.connect((self.host, self.port)) #esegue il 3WH 
            return True
        except Exception as e:
            print(f"{COLOR_RED}[ERRORE] Connessione al server {self.host}:{self.port} fallita: {e}{COLOR_RESET}")
            return False

    def send_line(self, line):
        try:
            msg = line.strip() + "\n"
            self.sock.sendall(msg.encode('utf-8'))
        except Exception:
            self.running = False

    def print_banner(self):
        print(f"{COLOR_CYAN}{COLOR_BOLD}")
        print("  ┌─────────────────────────────────────────┐")
        print("  │            FORZA 4 - CLIENT             │")
        print("  │     Laboratorio di Sistemi Operativi    │")
        print("  └─────────────────────────────────────────┘")
        print(f"{COLOR_RESET}")

    def render_board(self):
        print("\n    1   2   3   4   5   6   7")
        print("  ┌───┬───┬───┬───┬───┬───┬───┐")
        for r in range(ROWS):
            row_str = "  │"
            for c in range(COLS):
                val = self.board[r][c]
                if val == 'X':
                    cell = f" {COLOR_YELLOW}{COLOR_BOLD}X{COLOR_RESET} "
                elif val == 'O':
                    cell = f" {COLOR_RED}{COLOR_BOLD}O{COLOR_RESET} "
                else:
                    cell = "   "
                row_str += cell + "│"
            print(row_str)
            if r < ROWS - 1:
                print("  ├───┼───┼───┼───┼───┼───┼───┤")
        print("  └───┴───┴───┴───┴───┴───┴───┘")
        sym_color = COLOR_YELLOW if self.my_symbol == 'X' else COLOR_RED
        print(f"  Tuo simbolo: {sym_color}{COLOR_BOLD}{self.my_symbol}{COLOR_RESET} | Avversario: {COLOR_CYAN}{self.opponent_name}{COLOR_RESET}")

    def parse_board_string(self, board_str):
        if len(board_str) != ROWS * COLS:
            return
        idx = 0
        for r in range(ROWS):
            for c in range(COLS):
                ch = board_str[idx]
                self.board[r][c] = ' ' if ch == '.' else ch
                idx += 1

    def receive_loop(self):
        buffer = ""
        while self.running:
            try:
                data = self.sock.recv(1024)
                if not data:
                    print(f"\n{COLOR_RED}[DISCONNESSIONE] Connessione chiusa dal server.{COLOR_RESET}")
                    self.running = False
                    break
                buffer += data.decode('utf-8', errors='ignore')
                while '\n' in buffer:
                    line, buffer = buffer.split('\n', 1)
                    line = line.strip()
                    if line:
                        self.handle_server_message(line)
            except Exception:
                self.running = False
                break

    def handle_server_message(self, line):
        tokens = line.split()
        cmd = tokens[0]

        # Esempio: line = "OK Stanza 1 creata con successo"
        # line[3:] salta "OK " e prende solo "Stanza 1 creata con successo"
        if cmd == "OK":
            msg = line[3:]
            print(f"\n{COLOR_GREEN}[SERVER]{COLOR_RESET} {msg}")
            if self.state == "LOGIN":
                print(f"{COLOR_CYAN}--- Stanze attualmente disponibili ---{COLOR_RESET}")
                self.authenticated = True
                self.state = "LOBBY"
                self.auth_event.set()
            elif self.state == "GAME_OVER":
                if "lobby" in msg.lower() or "conclusa" in msg.lower() or "rifiutato" in msg.lower() or "accordo" in msg.lower():
                    self.state = "LOBBY"
                    self.current_room_id = -1
                    self.my_symbol = None
                    self.opponent_name = ""
                    self.is_my_turn = False
                    print(f"{COLOR_CYAN}[LOBBY] Sei tornato nella lobby.\n> {COLOR_RESET}", end="", flush=True)

        # Esempio: line = "ERR Colonna piena, riprova"
        # line[4:] salta "ERR " e prende solo "Colonna piena, riprova"
        elif cmd == "ERR":
            msg = line[4:]
            print(f"\n{COLOR_RED}[ERRORE]{COLOR_RESET} {msg}")
            if self.state == "LOGIN":
                self.authenticated = False
                self.auth_event.set()

        # Esempio: line = "LOBBY_ROOM 1 WAITING Mario"
        # tokens: ['LOBBY_ROOM', '1', 'WAITING', 'Mario']
        elif cmd == "LOBBY_ROOM":
            if len(tokens) >= 4:
                rid, status, owner = tokens[1], tokens[2], tokens[3]
                print(f"  Stanza #{rid:<3} | Stato: {status:<11} | Creatore: {owner}")

        # Esempio: line = "ROOM_CREATED 3"
        # tokens: ['ROOM_CREATED', '3'] -> l'id assegnato alla stanza appena creata
        elif cmd == "ROOM_CREATED":
            self.current_room_id = int(tokens[1])
            print(f"\n{COLOR_GREEN}[STANZA]{COLOR_RESET} Stanza #{self.current_room_id} creata. In attesa di avversari...")

        # Esempio: line = "ROOM_NOTIF 1 PLAYING Luigi"
        # Notifica broadcast per chi è in lobby: la stanza 1 è passata a stato PLAYING
        elif cmd == "ROOM_NOTIF":
            if len(tokens) >= 4 and self.state == "LOBBY":
                rid, st, own = tokens[1], tokens[2], tokens[3]
                print(f"\n{COLOR_GRAY}[INFO LOBBY] Stanza #{rid} ({own}) -> {st}{COLOR_RESET}")

        # Esempio: line = "JOIN_REQUEST 1 Andrea"
        # Un altro giocatore (Andrea) chiede al proprietario di entrare nella stanza 1
        elif cmd == "JOIN_REQUEST":
            rid = tokens[1]
            applicant = tokens[2]
            print(f"\n{COLOR_YELLOW}{COLOR_BOLD}[RICHIESTA SFIDA]{COLOR_RESET} Il giocatore '{applicant}' vuole entrare nella tua stanza #{rid}!")
            print(f"Per decidere digita: {COLOR_BOLD}accetta {rid}{COLOR_RESET} oppure {COLOR_BOLD}rifiuta {rid}{COLOR_RESET}")

        # Esempio: line = "JOIN_REJECTED 1"
        # Il proprietario della stanza 1 ha rifiutato la nostra richiesta di ingresso
        elif cmd == "JOIN_REJECTED":
            print(f"\n{COLOR_RED}[RIFIUTO]{COLOR_RESET} La richiesta per la stanza #{tokens[1]} e stata respinta dal proprietario.")
            self.state = "LOBBY"

        # Esempio: line = "GAME_START 1 Andrea X 1"
        # tokens: ['GAME_START', '1', 'Andrea', 'X', '1']
        # tokens[1] = id stanza ('1')
        # tokens[2] = nome avversario ('Andrea')
        # tokens[3] = mio simbolo assegnato ('X')
        # tokens[4] = turno iniziale: '1' significa tocca a me, '0' tocca all'avversario
        elif cmd == "GAME_START":
            self.current_room_id = int(tokens[1])
            self.opponent_name = tokens[2]
            self.my_symbol = tokens[3]
            self.is_my_turn = (tokens[4] == "1")
            self.state = "IN_GAME"
            self.board = [[' ' for _ in range(COLS)] for _ in range(ROWS)]

            print(f"\n{COLOR_GREEN}{COLOR_BOLD}=== PARTITA INIZIATA! ==={COLOR_RESET}")
            self.render_board()
            if self.is_my_turn:
                print(f"{COLOR_YELLOW}E il tuo turno! Inserisci un numero da 1 a 7 per la colonna:{COLOR_RESET} ", end="", flush=True)
            else:
                print(f"{COLOR_GRAY}In attesa della prima mossa di {self.opponent_name}...{COLOR_RESET}")

        # Esempio: line = "BOARD_UPDATE 4 MOVE X ...........................X................"
        # tokens[1] = colonna giocata ('4')
        # tokens[3] = chi ha giocato ('X')
        # tokens[4] = stringa di 42 caratteri che rappresenta l'intera scacchiera
        elif cmd == "BOARD_UPDATE":
            if len(tokens) >= 5:
                col = tokens[1]
                player_sym = tokens[3]
                board_str = tokens[4]
                self.parse_board_string(board_str)
                self.render_board()

        # Esempio: line = "YOUR_TURN"
        # Il server ci avvisa che l'avversario ha finito: tocca a noi fare una mossa
        elif cmd == "YOUR_TURN":
            self.is_my_turn = True
            print(f"\n{COLOR_YELLOW}{COLOR_BOLD}>> E il tuo turno! Scegli una colonna (1-7):{COLOR_RESET} ", end="", flush=True)

        # Esempio: line = "WAIT_TURN"
        # Il server ci avvisa che la nostra mossa e andata a buon fine: ora tocca attendere l'avversario
        elif cmd == "WAIT_TURN":
            self.is_my_turn = False
            print(f"{COLOR_GRAY}Turno avversario in corso...{COLOR_RESET}")

        # Esempio: line = "GAME_OVER WIN Complimenti, hai vinto!"
        # tokens[1] = esito ('WIN', 'LOSE', 'DRAW')
        # tokens[2:] = testo del messaggio (es. "Complimenti, hai vinto!")
        elif cmd == "GAME_OVER":
            outcome = tokens[1]
            msg = " ".join(tokens[2:])

            if outcome == "WIN":
                self.state = "GAME_OVER"
                print(f"\n{COLOR_GREEN}{COLOR_BOLD}🏆 {msg}{COLOR_RESET}")
            elif outcome == "LOSE":
                print(f"\n{COLOR_RED}{COLOR_BOLD}❌ {msg}{COLOR_RESET}")
                self.state = "LOBBY"
                self.current_room_id = -1
                self.my_symbol = None
                self.opponent_name = ""
                self.is_my_turn = False
                print(f"\n{COLOR_CYAN}{COLOR_BOLD}[LOBBY]{COLOR_RESET} Sei tornato nella lobby.")
                print(f"Digita {COLOR_BOLD}'lista'{COLOR_RESET} per vedere le stanze o {COLOR_BOLD}'crea'{COLOR_RESET} per crearne una nuova.\n> ", end="", flush=True)
            else:
                self.state = "GAME_OVER"
                print(f"\n{COLOR_YELLOW}{COLOR_BOLD}⚖️ {msg}{COLOR_RESET}")

        # Esempio: line = "REMATCH_OFFER Vuoi fare la rivincita?"
        # line[14:] salta "REMATCH_OFFER " e prende la domanda
        elif cmd == "REMATCH_OFFER":
            msg = line[14:]
            print(f"\n{COLOR_CYAN}{COLOR_BOLD}[OPZIONE NUOVA SESSIONE]{COLOR_RESET} {msg}")
            print(f"Rispondi con: {COLOR_BOLD}rematch si{COLOR_RESET} o {COLOR_BOLD}rematch no{COLOR_RESET}")

        # Esempio: line = "REMATCH_OK Rivincita rifiutata. Stanza chiusa."
        # line[11:] salta "REMATCH_OK " e prende la spiegazione
        elif cmd == "REMATCH_OK":
            msg = line[11:]
            print(f"\n{COLOR_GREEN}[SESSIONE]{COLOR_RESET} {msg}")
            self.state = "LOBBY"
            self.current_room_id = -1
            self.my_symbol = None
            self.opponent_name = ""
            self.is_my_turn = False
            print(f"{COLOR_CYAN}[LOBBY] In attesa di nuovi sfidanti...\n> {COLOR_RESET}", end="", flush=True)

        # Esempio: line = "OPPONENT_DISCONNECTED"
        # Il socket dell'avversario e caduto, la partita viene interrotta d'ufficio
        elif cmd == "OPPONENT_DISCONNECTED":
            print(f"\n{COLOR_RED}[AVVISO] L'avversario si e disconnesso. La partita e terminata.{COLOR_RESET}")
            self.state = "LOBBY"
            self.current_room_id = -1
            self.my_symbol = None
            self.opponent_name = ""
            self.is_my_turn = False
            print(f"{COLOR_CYAN}[LOBBY] Sei tornato nella lobby.\n> {COLOR_RESET}", end="", flush=True)

        # Esempio: line = "BYE"
        # Risposta finale del server prima di chiudere la socket a seguito di QUIT
        elif cmd == "BYE":
            self.running = False

    def run(self):
        self.print_banner()

        if not self.connect():
            return

        # Ricevitore thread
        t = threading.Thread(target=self.receive_loop, daemon=True)
        t.start()

        # Login con sincronizzazione su conferma del server
        while self.running and not self.authenticated:
            try:
                name = input("Inserisci il tuo nickname: ").strip()
            except (KeyboardInterrupt, EOFError):
                self.running = False
                return

            if name:
                if " " in name:
                    print(f"{COLOR_RED}Il nickname non puo contenere spazi.{COLOR_RESET}")
                    continue

                self.auth_event.clear()
                self.username = name
                self.send_line(f"NICK {name}")
                # Attende la risposta OK o ERR dal thread di ricezione
                self.auth_event.wait(timeout=3.0)
                if not self.running:
                    return
                if self.authenticated:
                    break

        # Piccola pausa per consentire la visualizzazione ordinata delle stanze iniziali
        time.sleep(0.15)
        self.print_help()

        # Input loop principale
        while self.running:
            try:
                user_input = input().strip()
                if not user_input:
                    continue

                parts = user_input.split()
                action = parts[0].lower()

                if self.state == "IN_GAME":
                    if action.isdigit():
                        if len(parts) > 1:
                            print(f"{COLOR_RED}Inserisci un solo numero da 1 a 7.{COLOR_RESET}")
                            continue
                        col = int(action)
                        if 1 <= col <= 7:
                            self.send_line(f"MOVE {self.current_room_id} {col}")
                        else:
                            print(f"{COLOR_RED}Colonna non valida. Scegli tra 1 e 7.{COLOR_RESET}")
                    elif action in ("abbandona", "leave"):
                        if len(parts) > 1:
                            print(f"{COLOR_RED}Il comando 'abbandona' non accetta parametri.{COLOR_RESET}")
                            continue
                        self.send_line(f"LEAVE {self.current_room_id}")
                        self.state = "LOBBY"
                        self.current_room_id = -1
                        self.my_symbol = None
                        self.opponent_name = ""
                        self.is_my_turn = False
                        print(f"\n{COLOR_CYAN}[LOBBY]{COLOR_RESET} Hai abbandonato la partita e sei tornato nella lobby.\n> ", end="", flush=True)
                    else:
                        print(f"{COLOR_GRAY}Inserisci il numero della colonna (1-7) o 'abbandona'.{COLOR_RESET}")

                elif self.state == "GAME_OVER":
                    if action in ["esci", "quit"]:
                        if len(parts) > 1:
                            print(f"{COLOR_RED}Il comando 'esci' non accetta parametri.{COLOR_RESET}")
                            continue
                        self.send_line(f"REMATCH {self.current_room_id} NO")
                        self.send_line("QUIT")
                        self.running = False
                        break
                    elif action == "rematch":
                        if len(parts) == 2:
                            choice = parts[1].lower()
                            if choice in ["si", "yes", "s"]:
                                self.send_line(f"REMATCH {self.current_room_id} YES")
                            elif choice in ["no", "n"]:
                                self.send_line(f"REMATCH {self.current_room_id} NO")
                                self.state = "LOBBY"
                                self.current_room_id = -1
                                self.my_symbol = None
                                self.opponent_name = ""
                                self.is_my_turn = False
                                print(f"\n{COLOR_CYAN}{COLOR_BOLD}[LOBBY]{COLOR_RESET} Sei tornato nella lobby.")
                                print(f"Digita {COLOR_BOLD}'lista'{COLOR_RESET} o {COLOR_BOLD}'crea'{COLOR_RESET}.\n> ", end="", flush=True)
                            else:
                                print(f"{COLOR_RED}Scelta non valida. Digita 'rematch si' oppure 'rematch no'.{COLOR_RESET}")
                        else:
                            print(f"{COLOR_RED}Uso: rematch si oppure rematch no.{COLOR_RESET}")
                    else:
                        print("Digita 'rematch si', 'rematch no' o 'esci'.")

                else:
                    # Comandi Lobby con validazione rigorosa dei parametri
                    if action in ("crea", "create"):
                        if len(parts) > 1:
                            print(f"{COLOR_RED}Il comando 'crea' non accetta parametri. Digita solo 'crea'.{COLOR_RESET}")
                        else:
                            self.send_line("CREATE")
                    elif action in ("lista", "list"):
                        if len(parts) > 1:
                            print(f"{COLOR_RED}Il comando 'lista' non accetta parametri. Digita solo 'lista'.{COLOR_RESET}")
                        else:
                            print(f"\n{COLOR_CYAN}--- Stanze disponibili sul server ---{COLOR_RESET}")
                            self.send_line("LIST")
                    elif action in ("unisciti", "join"):
                        if len(parts) == 2 and parts[1].isdigit():
                            rid = parts[1]
                            self.send_line(f"JOIN {rid}")
                        else:
                            print(f"{COLOR_RED}Uso: unisciti <id_stanza>{COLOR_RESET}")
                    elif action in ("accetta", "accept"):
                        if len(parts) == 2 and parts[1].isdigit():
                            self.send_line(f"ACCEPT {parts[1]}")
                        else:
                            print(f"{COLOR_RED}Uso: accetta <id_stanza>{COLOR_RESET}")
                    elif action in ("rifiuta", "reject"):
                        if len(parts) == 2 and parts[1].isdigit():
                            self.send_line(f"REJECT {parts[1]}")
                        else:
                            print(f"{COLOR_RED}Uso: rifiuta <id_stanza>{COLOR_RESET}")
                    elif action in ["chiudi", "close"]:
                        if len(parts) == 2 and parts[1].isdigit():
                            self.send_line(f"LEAVE {parts[1]}")
                        else:
                            print(f"{COLOR_RED}Uso: chiudi <id_stanza>{COLOR_RESET}")
                    elif action in ("esci", "quit"):
                        if len(parts) > 1:
                            print(f"{COLOR_RED}Il comando 'esci' non accetta parametri. Digita solo 'esci'.{COLOR_RESET}")
                        else:
                            self.send_line("QUIT")
                            self.running = False
                            break
                    elif action in ("aiuto", "help"):
                        if len(parts) > 1:
                            print(f"{COLOR_RED}Il comando 'aiuto' non accetta parametri. Digita solo 'aiuto'.{COLOR_RESET}")
                        else:
                            self.print_help()
                    else:
                        print(f"{COLOR_GRAY}Comando sconosciuto. Digita 'aiuto' per visualizzare i comandi.{COLOR_RESET}")

            except (KeyboardInterrupt, EOFError):
                self.send_line("QUIT")
                self.running = False
                break

        print("\nUscita dall'applicazione.")
        try:
            self.sock.close()
        except Exception:
            pass

    def print_help(self):
        with self.lock:
            print(f"\n{COLOR_CYAN}{COLOR_BOLD}Comandi disponibili:{COLOR_RESET}")
            print("  crea              -> Crea una nuova stanza di gioco")
            print("  lista             -> Visualizza tutte le stanze e il loro stato")
            print("  unisciti <id>     -> Richiedi di entrare in una stanza in attesa")
            print("  accetta <id>      -> Accetta la richiesta di uno sfidante")
            print("  rifiuta <id>      -> Rifiuta la richiesta di uno sfidante")
            print("  chiudi <id>       -> Chiudi una stanza creata da te")
            print("  esci              -> Chiudi la connessione ed esci")
            print("  aiuto             -> Mostra questo messaggio\n")

if __name__ == "__main__":
    host = os.environ.get("SERVER_HOST", "127.0.0.1")
    port = int(os.environ.get("SERVER_PORT", "5050"))

    if len(sys.argv) >= 2:
        host = sys.argv[1]
    if len(sys.argv) >= 3:
        port = int(sys.argv[2])

    client = ConnectFourClient(host, port)
    client.run()

