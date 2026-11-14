# Progetto Forza 4 (Connect Four) - Client/Server

Progetto per l'esame di **Laboratorio di Sistemi Operativi** (Corso di Laurea in Informatica, Nuovo Ordinamento - 8 CFU).

Il sistema implementa un'applicazione distribuita client-server per il gioco del **Forza 4**:
- **Server**: sviluppato in linguaggio **C**, basato su socket di rete stream (TCP) e gestione concorrente dei client tramite **thread POSIX (`pthread`)**.
- **Client**: sviluppato in **Python 3**, dotato di interfaccia a riga di comando (CLI/TUI) con colori ANSI e visualizzazione della scacchiera a caratteri box.
- **Orchestrazione**: configurazione completa con **Docker-compose** per garantire la riproducibilità dell'ambiente di esecuzione su qualsiasi piattaforma.

---

## Struttura del Progetto

```text
Connect-Four/
├── docker-compose.yml       # Definizione dei servizi server e client
├── Makefile                 # Target di compilazione locale e scorciatoie docker
├── .gitignore
├── README.md                # Istruzioni d'uso e configurazione
│
├── server/
│   ├── Dockerfile           # Immagine Alpine Linux con build GCC del server
│   ├── Makefile             # Compilazione del binario server_forza4
│   ├── include/
│   │   ├── protocol.h       # Costanti di protocollo, stati e dimensioni
│   │   ├── game.h           # Header della logica di gioco (griglia 6x7)
│   │   ├── session.h        # Gestione stanze, client e sincronizzazione thread
│   │   └── client_handler.h # Ciclo di vita del thread per singolo client
│   └── src/
│       ├── main.c           # Inizializzazione socket, bind, listen e accept loop
│       ├── game.c           # Regole di Forza 4, caduta pedine e verifica vittoria
│       ├── session.c        # Gestione concorrente con mutex delle stanze e broadcast
│       └── client_handler.c # Parsing del protocollo testuale e routing comandi
│
└── client/
    ├── Dockerfile           # Immagine Python 3 per esecuzione containerizzata
    └── client.py            # Client interattivo a terminale con thread di ascolto
```

---

## Requisiti

- **Docker** e **Docker Compose** (raccomandati)
*oppure*
- Sistema operativo Linux / WSL / macOS con:
  - `gcc` (supporto standard C99 e flag `-pthread`)
  - `make`
  - `python3` (versione 3.8 o superiore)

> **Nota per macOS e Linux**: su macOS e su molte distribuzioni Linux, il comando `python` potrebbe non essere presente nel PATH di default. Se riscontri l'errore `command not found: python`, utilizza **`python3`** (ad esempio: `python3 client/client.py` o `python3 test/test_race_conditions.py`).

---

## Esecuzione con Docker-compose

Il file `docker-compose.yml` include sia il servizio del server che quello del client.

### 1. Avvio del Server
In un primo terminale, avviare il container del server:

```bash
docker compose up --build server
```

Il server rimarrà in ascolto sulla porta standard `5050`.

### 2. Avvio dei Client Giocatori
Aprire due o più terminali separati (uno per ciascun giocatore) e lanciare un client interattivo:

```bash
# Terminale Giocatore 1
docker compose run --rm client

# Terminale Giocatore 2
docker compose run --rm client
```

Ogni terminale presenterà il prompt per l'inserimento del nickname e l'accesso alla lobby di gioco.

---

## Esecuzione Locale (senza Docker)

### 1. Compilazione ed esecuzione del Server
Dalla cartella principale del progetto:

```bash
make
./server/server_forza4 5050
```

### 2. Esecuzione dei Client
In terminali distinti:

```bash
python3 client/client.py 127.0.0.1 5050
```

---

## Funzionalità e Comandi del Gioco

Una volta effettuato l'accesso con il proprio nickname, il client consente di interagire con il server tramite comandi testuali:

### Comandi in Lobby
- `crea`: registra una nuova stanza di gioco sul server (assegnandole un identificativo univoco) e la pone in stato di attesa.
- `lista`: richiede e stampa l'elenco aggiornato di tutte le stanze e il loro stato (`IN_ATTESA`, `IN_CORSO`, `TERMINATA`).
- `unisciti <id>`: invia una richiesta di partecipazione alla stanza specificata.
- `accetta <id>`: (riservato al creatore della stanza) accetta la richiesta dello sfidante e avvia la partita.
- `rifiuta <id>`: (riservato al creatore della stanza) rifiuta la richiesta dello sfidante.
- `chiudi <id>`: (riservato al creatore della stanza) annulla e chiude una stanza creata in precedenza.
- `esci`: chiude la connessione con il server.
- `aiuto`: riepiloga i comandi disponibili.

### Comandi durante la Partita
- Durante il proprio turno, digitare un numero da **`1` a `7`** per inserire la pedina nella colonna corrispondente.
- `abbandona`: abbandona la partita in corso comunicando la chiusura della sessione all'avversario.

### Gestione Fine Partita e Rivincita
- Al termine della partita (vittoria o pareggio), il server notifica l'esito ai due giocatori e aggiorna lo stato per gli altri utenti in lobby.
- **In caso di vittoria**: il vincitore può scegliere se avviare una nuova sessione (`rematch si`), diventando il proprietario della stanza che torna in stato di attesa per nuovi sfidanti; il perdente viene ricondotto in lobby.
- **In caso di pareggio**: entrambi i giocatori possono concordare congiuntamente l'avvio di una nuova sessione (`rematch si`).

---

## Test di Concorrenza, Race Condition e Casi Limite

Nella cartella `test/` sono disponibili suite automatizzate per verificare la robustezza della sincronizzazione, la gestione della memoria e l'assenza di race condition:

### 1. Test di Concorrenza e Stress Test
```bash
python test/test_race_conditions.py
# (Su macOS/Linux usare python3 se 'python' non è presente)
```
Lo script esegue quattro scenari ad alta concorrenza utilizzando barriere di sincronizzazione (`threading.Barrier`):
1. **Creazione concorrente di stanze**: 15 client richiedono la creazione di stanze nello stesso millisecondo; verifica l'univocità progressiva degli ID e l'integrità del mutex.
2. **Join simultaneo sulla stessa stanza**: 10 client tentano di unirsi simultaneamente alla medesima stanza; il lock serializza le richieste ammettendone solo una e respingendo le altre.
3. **Turn Enforcement**: invio concorrente di mosse tra i due giocatori; verifica che il giocatore fuori turno venga bloccato e lo stato di gioco rimanga consistente.
4. **Stress test disconnessioni**: 25 client si connettono, inviano comandi e si disconnettono bruscamente per testare la pulizia delle risorse e l'assenza di crash o deadlock.

### 2. Test Casi Limite ed Edge Cases
```bash
python test/test_edge_cases.py
```
Verifica:
- **Controllo autorizzativo anti-tampering**: impedisce a utenti non autorizzati di chiudere stanze altrui.
- **Feedback bidirezionale**: verifica che il creatore riceva conferma del rifiuto e lo sfidante riceva la notifica.
- **Abbandono volontario asimmetrico**: verifica che l'avversario riceva la notifica di disconnessione e chi abbandona riceva la conferma di uscita.
- **Univocità nickname case-insensitive**: garantisce che nickname con maiuscole/minuscole diverse (es. `Luigi` e `LUIGI`) vengano correttamente rilevati come duplicati.

### 3. Dimostrazione Visuale Affiancata (Split-Screen)
```bash
python test/demo_live_visuale.py
```
Simula sul terminale due client affiancati (Alice e Bob) mostrando passo dopo passo in due colonne parallele l'interazione in tempo reale, i messaggi di server, i controlli di sicurezza e l'abbandono.

### 4. Analisi con Sanitizer GCC (TSan e ASan)
Per un controllo approfondito a livello di memoria e thread:
```bash
# ThreadSanitizer (rilevamento data races tra thread):
make tsan

# AddressSanitizer & UndefinedBehaviorSanitizer (buffer overflow e memory leak):
make asan
```

---

## Simulazione Partita e Test delle Casistiche di Vittoria

Nella cartella `test/` è disponibile lo script interattivo `simulate_game.py` che permette di simulare partite complete e testare:
- **Vittoria Orizzontale**
- **Vittoria Verticale**
- **Diagonale Discendente (`\`)**
- **Diagonale Ascendente (`/`)**
- **Partita Patta / Pareggio (42 mosse)** e gestione completa della negoziazione di rivincita (errore di sintassi, voto favorevole e rifiuto).

### Esecuzione Automatica con Rendering delle Scacchiere
Per visualizzare le scacchiere e validare tutte le casistiche in sequenza:

```bash
python test/simulate_game.py --test-all
```

### Dimostrazione Interattiva su 3 Terminali Separati
```bash
python test/simulate_game.py
```
Lo script apre automaticamente **3 finestre di terminale** distinte (Server, Giocatore 1 Giallo, Giocatore 2 Rosso), connette i client, crea la stanza e riproduce la partita mossa dopo mossa con ritardo visivo, mostrando la caduta delle pedine e l'esito finale.
