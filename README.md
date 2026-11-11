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

- **Docker** e **Docker Compose** (raccomandati per l'esame da 8 CFU)
*oppure*
- Sistema operativo Linux / WSL / macOS con:
  - `gcc` (supporto standard C99 e flag `-pthread`)
  - `make`
  - `python3` (versione 3.8 o superiore)

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
- `esci`: chiude la connessione con il server.
- `aiuto`: riepiloga i comandi disponibili.

### Comandi durante la Partita
- Durante il proprio turno, digitare un numero da **`1` a `7`** per inserire la pedina nella colonna corrispondente.
- `abbandona`: abbandona la partita in corso comunicando la chiusura della sessione all'avversario.

### Gestione Fine Partita e Requisito Opzionale
- Al termine della partita (vittoria o pareggio), il server notifica l'esito ai due giocatori e aggiorna lo stato per gli altri utenti in lobby.
- **In caso di vittoria**: il vincitore può scegliere se avviare una nuova sessione (`rematch si`), diventando il proprietario della stanza che torna in stato di attesa per nuovi sfidanti; il perdente viene ricondotto in lobby.
- **In caso di pareggio**: entrambi i giocatori possono concordare congiuntamente l'avvio di una nuova sessione (`rematch si`).

---

## Test di Concorrenza e Race Condition

Nella cartella `test/` è disponibile una suite automatizzata per verificare la robustezza della sincronizzazione e l'assenza di race condition:

```bash
# Avviare prima il server (in locale o via Docker)
# Poi eseguire i test:
python test/test_race_conditions.py
```

Lo script esegue quattro scenari ad alta concorrenza utilizzando barriere di sincronizzazione (`threading.Barrier`):
1. **Creazione concorrente di stanze**: 15 client richiedono la creazione di stanze nello stesso millisecondo; verifica che non vi siano ID duplicati o corruzioni nell'array delle stanze.
2. **Join simultaneo sulla stessa stanza**: 10 client tentano di unirsi simultaneamente alla medesima stanza; il lock serializza le richieste ammettendone solo una e respingendo le altre.
3. **Turn Enforcement**: invio concorrente di mosse tra i due giocatori; verifica che il giocatore fuori turno venga bloccato e lo stato di gioco rimanga consistente.
4. **Stress test disconnessioni**: 25 client si connettono, inviano comandi e si disconnettono bruscamente per testare la pulizia delle risorse e l'assenza di crash o deadlock.

Per un controllo approfondito a livello di memoria, il server può essere compilato con **ThreadSanitizer** (TSan):

```bash
cd server && make clean && make tsan
```

---

## Simulazione Partita e Test delle Casistiche di Vittoria

Nella cartella `test/` è disponibile lo script interattivo `simulate_game.py` che permette di simulare partite complete e testare:
- **Vittoria Orizzontale**
- **Vittoria Verticale**
- **Diagonale Discendente (`\`)**
- **Diagonale Ascendente (`/`)**
- **Partita Patta / Pareggio (42 mosse)**

### 1. Dimostrazione Visiva su 3 Terminali Separati
Lanciando semplicemente:

```bash
python test/simulate_game.py
```

Lo script apre automaticamente **3 finestre di terminale** distinte (Server, Giocatore 1 Giallo, Giocatore 2 Rosso), connette i client, crea la stanza e riproduce la partita mossa dopo mossa con ritardo visivo, mostrando la caduta delle pedine e l'esito finale.

È anche possibile passare direttamente la casistica desiderata da riga di comando:
```bash
python test/simulate_game.py orizzontale
python test/simulate_game.py verticale
python test/simulate_game.py diag_down
python test/simulate_game.py diag_up
python test/simulate_game.py pareggio
```

### 2. Esecuzione Automatica di Tutte le Casistiche (Headless)
Per validare tutte e 5 le casistiche contemporaneamente in modalità batch:

```bash
python test/simulate_game.py --test-all
```
