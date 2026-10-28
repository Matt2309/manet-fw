# manet-fw

Firmware per la **Heltec Wireless Tracker V1.1** (ESP32-S3FN8). Le schede trasmettono beacon
ESP-NOW come il Blocco 3 di `manet-sim` e registrano tutto su LittleFS; l'analisi si fa dopo,
offline. La logica (pacchetti, vicini, rilevatore, corsa, tabella del gruppo, codec BLE) e' C++ puro in `lib/core`, testata
con `pio test -e native` (compresa una rete di `Node` in memoria); in `src/` restano gli adattatori verso l'hardware.
Lo stesso firmware gira su tutte le schede: il ruolo (corridore o capogruppo) si sceglie con il tasto.

## Compilare e caricare

```
python3 -m venv .venv && .venv/bin/pip install -r tools/requirements.txt platformio
.venv/bin/pio run -e tracker -t upload        # una scheda alla volta
.venv/bin/pio device monitor                  # 115200: il MAC compare nelle righe "# ..."
.venv/bin/pio test -e native                  # test della logica (senza hardware)
.venv/bin/python -m pytest tools              # test degli script Python
```

Si usa il fork `pioarduino` della piattaforma (core Arduino 3.3.x, ESP-IDF 5.5): quella
ufficiale espressif32 e' ferma al core 2.x. La definizione della scheda e' in
`boards/`, perche' PlatformIO non ne ha una per il Tracker.

## Prima di partire

1. Flash di ogni scheda, lettura del MAC dalla seriale (o dal display, con ID 0).
2. Aggiungere i MAC a `include/nodes.h` (ID 1..20) e ricaricare: la scheda mostra il suo `ID`.
   Il ruolo (corridore o capogruppo) non sta in `nodes.h`: vedi sotto.
3. Parametri (canale, potenza, periodo, soglie) in `include/config.h`.
4. `BAT_CAL` in `config.h` va tarato con un multimetro sulla batteria.

## Uso

La registrazione parte da sola all'accensione, su un nuovo `/run_NNN.csv`.

| Tasto USER | Effetto |
|---|---|
| pressione breve (< 1 s) | riga `EVT` numerata, il LED lampeggia, lo schermo si accende |
| pressione lunga (3 s) iniziata nei primi 10 s dopo l'accensione | alterna `CORR`/`CAPO` e lo salva in NVS |
| pressione lunga (3 s), a corsa avviata | attiva l'**SOS**; un'altra pressione lunga lo annulla |

Il ruolo resta quello salvato anche dopo un riavvio (alla prima accensione e' `CORR`) e si cambia
solo prima dello START. Non tenere premuto il tasto durante l'accensione: GPIO0 e' il pin di avvio.
La finestra conta dall'inizio della pressione: contano anche i 3 s della pressione, che puo' scattare dopo i 10 s.
Il registro resta sempre attivo: la pressione lunga non chiude piu' il file.

**Display** (160x80): ID, ruolo, stato della corsa (`PRONTA`, `CORSA mm:ss`, `FERMA mm:ss`), GPS, salti dal capogruppo e
compagni sentiti, eventi e batteria. Avvisi in grande: `SOS INVIATO`, `MEMORIA PIENA`, `ISOLATO` (corsa avviata e nessuna via
verso il capogruppo). Lo schermo resta acceso `DISPLAY_ON_MS` dopo un tasto o un avviso nuovo (all'avvio per la finestra del ruolo).
Nella finestra del ruolo la riga 3 mostra `premi 3s=ruolo (Ns)`; un capogruppo che sente un altro capogruppo `DUE CAPOGRUPPO`.

Sotto il 5% di spazio libero la scheda smette di scrivere e mostra `MEMORIA PIENA`
(circa 5 ore di registrazione). Con un distacco della batteria si perdono al massimo 5 s.

## Corsa e percorso dei dati

- **START / STOP** arrivano dall'app al capogruppo (BLE), oppure, per provare senza app, dalla seriale del capogruppo
  (`start`, `stop`). Il capogruppo fissa T0 e manda il comando a tutti; chi lo riceve lo rilancia una volta aggiornando i
  decimi di secondo trascorsi. Il beacon porta i secondi da T0 e il bit «ferma»: chi ha perso lo START (o lo STOP) lo
  ricava dal primo compagno che lo conosce. Uno START a corsa gia' avviata non azzera T0.
- **Salti**: il capogruppo dichiara 0, gli altri 1 + il minimo fra i compagni «sentiti bene» (stima sopra -88 dBm, sentiti
  negli ultimi 2 s); chi non sente nessuno dichiara 255.
- **Telemetria**: ogni corridore produce un record al secondo e lo mette nella **coda unica** (600 record, 10 minuti), dove
  finiscono anche i record dei compagni. La coda si svuota verso il compagno con meno salti e segnale stimato piu' forte,
  con invio unicast e conferma ESP-NOW: fino a 3 tentativi, poi il secondo candidato; la scelta si rifa' a ogni invio.
  Senza candidati i record restano in coda. A coda piena si produce un record ogni 5 s sovrascrivendo i piu' vecchi; i record
  con SOS non si cancellano mai. La coppia (id, secondi da T0) elimina i doppioni.
- **Allarmi**: a ogni cambio del rilevatore (pre-allarme, perso, fine) parte un allarme a tutti, rilanciato una volta da chi
  lo riceve; l'SOS si ripete ogni 10 s. Gli allarmi viaggiano anche dentro la telemetria.
- **Prima dello START** le schede mandano solo beacon: niente telemetria e niente allarmi.

## Capogruppo e BLE

Il capogruppo tiene una **tabella del gruppo** (una riga per corridore, compreso se stesso, aggiornata da beacon, allarmi e
telemetria; un corridore sentito resta fino allo STOP) e parla con l'app via BLE. Stato di un corridore, per precedenza:
SOS, perso, pre-allarme, ok. Il pre-allarme vale con un osservatore. Il **perso** lo confermano al massimo 2 schede (`LOST_MIN_OBSERVERS`),
ma non piu' di quante vedono il corridore: se lo vede una sola scheda basta quella; se lo vedono in due e una lo sente bene resta un
sospetto (pre-allarme). R e' la media dei segnali fusi riportati negli ultimi 5 s.

Chiudere un allarme dall'app resta sul capogruppo: non si ripropone finche' il corridore non torna normale e poi si
stacca di nuovo (un SOS chiuso, finche' il corridore non smette di segnalarlo). Un allarme che si spegne da solo manda
l'evento RIENTRO; l'SOS si chiude solo a mano.

**BLE** (libreria del core Arduino, NimBLE; si accende solo se la scheda e' capogruppo): un servizio con tre
caratteristiche, UUID in `include/config.h` (`BLE_*_UUID`, da usare uguali nell'app), MTU 247:

| caratteristica | verso | contenuto |
|---|---|---|
| dati | nodo -> app, notify | STATO ogni 5 s (`seq` 0 e `t` riservato prima dello START), CONFERMA |
| urgenti | nodo -> app, notify | EVENTO subito |
| comandi | app -> nodo, write | START, STOP, CHIUDI_ALLARME, RIPRENDI_DA |

Il formato dei byte e' quello di `docs/protocollo_ble.md` di manet-mobile e lo conosce solo `lib/core/ble_codec`.
L'intervallo di connessione e' lungo (`BLE_CONN_*`, provvisorio: da confermare con la prova F1).

**File della corsa**: a ogni START il capogruppo apre `/corsa_NNN.bin` e vi scrive, sincronizzando su flash, ogni STATO e
EVENTO con seq **prima** di mandarlo. `RIPRENDI_DA(n)`: CONFERMA con l'ultimo seq disponibile, poi rimanda in ordine i
messaggi con seq maggiore di n mentre quelli nuovi continuano a partire. Se il capogruppo si riavvia a corsa in corso
(bandiera in NVS) riapre il file e riprende dall'ultimo seq; T0 lo ricava dai beacon dei compagni. I file `corsa_*.bin` si
scaricano come i `run_*.csv` (`get`, `rm`).

### Prova F1 (il BLE disturba la ricezione dei beacon?)

Con il capogruppo acceso e le altre schede vicine (anche senza START: lo STATO parte comunque ogni 5 s), collegare l'app o
nRF Connect per qualche minuto, scollegare, ricollegare. Poi, sul file del capogruppo:

```
python tools/scarica.py
python tools/prova_f1.py dati/<data>/scheda_<id>/run_NNN.csv
```

Lo script usa le righe `BLE` (connesso/caduto) e `RX` e stampa, per ogni intervallo con BLE collegato o libero e per ogni
scheda, beacon ricevuti, attesi e persi %, piu' il totale per i due stati.

### Prova F2 (conferma a 1 Mbit/s)

```
.venv/bin/pio run -e tracker_f2 -t upload
```

Con questa build ogni scheda manda, una volta al secondo e a rotazione, un pacchetto da circa 223 byte in unicast con
conferma verso le altre, senza START. Nel registro, righe `TEL` `inviato`, `confermato` e `fallito` con il tentativo e il
tempo di conferma in ms (ultima colonna); chi riceve scrive `ricevuto`. Tre tentativi falliti e si ricomincia al secondo dopo.

## Scaricare i dati

```
python tools/scarica.py                 # tutte le schede collegate
python tools/scarica.py --porta /dev/cu.usbmodemXXXX
```

Salva in `dati/<data>/scheda_<id>/` controllando dimensione e CRC32; non cancella nulla.
**`get` sul file aperto lo chiude** e ne apre subito uno nuovo: il registro non si ferma.

Comandi seriali (115200, uno per riga):

| Comando | Risposta |
|---|---|
| `info` | `INFO id= mac= fw= file= state=` |
| `ls` | una riga `FILE nome dimensione crc32` per file, poi `END` |
| `free` | `FREE total= used= free=` |
| `get <file>` | `BEGIN nome dimensione crc32`, i byte del file, `END` |
| `rm <file>` | `OK`; rifiuta il file aperto |
| `format SI` | formatta e ferma la registrazione |
| `start` / `stop` | solo sul capogruppo: avvia o ferma la corsa (come il comando dell'app) |

Le righe che iniziano con `#` sono messaggi di debug e vanno ignorate.

## Riepilogo

```
python tools/riepilogo.py dati/2026-10-07
```

Stampa le statistiche per coppia e per scheda e l'allineamento sull'UTC del GPS, e scrive
`percorso.gpx` (media delle schede, 1 Hz, con `<ele>`: si usa come `track.gpx_file` di
`manet-sim`) e `rssi_coppie.png`.

## Formato dei file

Prima riga `# ...` con versione, id, ruolo, MAC, canale, potenza, periodo, protocollo; poi tre righe
`# param ...` con i parametri di `config.h`. Poi righe CSV, il primo campo e' il tipo e il secondo
`t_ms` (`millis()` della scheda):

```
TX,t_ms,seq
RX,t_ms,src,seq,rssi,tab,salti,t_s,stato   tab = id:segnale:eta:incertezza:pendenza;...  (eta' in 0,1 s, incertezza 255 = n.d.)
                                           t_s vuoto = corsa non avviata; con LOG_RX_REDUCED solo fino a rssi, tab vuoto
GPS,t_ms,utc,lat,lon,alt,vel_kmh,rotta,hdop,sat,fix     1 Hz, fix 0/2/3; senza fix i campi sono vuoti
EVT,t_ms,n
BAT,t_ms,mV                     ogni 30 s
SYS,t_ms,messaggio
KAL,t_ms,id,segnale,pendenza,incertezza,eta                stima del Kalman per ogni compagno sentito, 1 Hz
DET,t_ms,versione,tipo,inizio|fine,id,R,soglia             decisioni: versione base|gnss, tipo pre|perso|sorda; R e soglia in vigore solo per pre
TEL,t_ms,azione,id,t_s,compagno,n_coda,tentativi,ms        telemetria: prodotto|inviato|confermato|fallito|ricevuto|coda; ms = conferma
CMD,t_ms,comando,origine,seq,decimi                        START e STOP (origine app|radio)
BLE,t_ms,evento,dettaglio                                  connessioni e cadute, solo capogruppo (fase 5)
ALM,t_ms,azione,creatore,seq,corridore,tipo                allarmi creati, ricevuti e rilanciati (tipo pre|perso|sos|fine)
```

## Rilevatore

Lo stesso di `manet-sim` (`src/kalman.py`, `fusion.py`, `detector.py`), con i parametri di `config.h`
(tutti scritti nelle righe `# param`): un Kalman per compagno (stato segnale e pendenza, rumore di processo
0,25 dB/s2, di misura 10,84 dB2), fusione con le stime dei vicini lette dai loro beacon (voci piu' vecchie di
2 s escluse), pre-allarme con R sotto -93 dBm per 1 s, nodo perso dopo 10 s di silenzio, regola `not_deaf_episode`
(una scheda che smette di sentire tutti insieme non dichiara persi gli altri; riga `DET ... sorda`).
Lo spegnimento chiede 30 s con R sopra -90 dBm e almeno 48 beacon su 60 ricevuti dal compagno.
Il rilevatore gira sempre e registra le decisioni; l'invio degli allarmi arriva con la fase 4.

### Ramo GNSS

In parallelo al rilevatore senza GNSS gira una seconda versione del pre-allarme che cambia solo la soglia
d'inizio; entrambe si registrano (righe `DET` con `base` e `gnss`) e `ALARM_USES_GNSS` in `config.h` sceglie quale
conta per gli allarmi (di partenza: senza GNSS). Il GNSS non tocca mai il nodo perso, ne' lo spegnimento.

Un fix e' valido se e' 3D, con almeno 6 satelliti, HDOP al massimo 2,5 ed epoca piu' giovane di 2 s; il beacon
porta la posizione (lat, lon, HDOP) solo con fix valido, e la posizione di un compagno vale 2 s. Con fix valido su
entrambe le schede e almeno 10 s di campioni continui (uno al secondo), confrontando distanza e segnale fuso di 10 s fa:

| distanza (10 s) | segnale | soglia | conferma |
|---|---|---|---|
| cresce di oltre 8 m | cala di almeno 3 dB | -90 dBm | 1 s |
| varia al massimo di 3 m | cala di almeno 3 dB | -95 dBm | 3 s |
| altro (3-8 m, diminuisce, segnale che non cala) | | -93 dBm | 1 s |

Senza fix valido su una delle due: -93 dBm. Tutte le soglie sono in `config.h` (`GNSS_*`). Sul display
`GPS OK n sat` indica il fix valido; `GPS 3D n sat` un fix che non basta.

Sul display, con allarmi attivi, la riga 3 mostra `PRE n PERSI n`.

**Confronto con il simulatore.** `test/test_sim_compare` fa girare 5 `Node` su due corse simulate e confronta
gli allarmi con quelli del rilevatore Python (stessi episodi, istanti entro 1 tick di 100 ms). Le corse si
rigenerano con l'ambiente del simulatore, che non viene modificato:

```
PYTHONDONTWRITEBYTECODE=1 ~/manet-sim/venv/bin/python tools/esporta_sim.py sep  --out test/test_sim_compare/sim_sep.csv
PYTHONDONTWRITEBYTECODE=1 ~/manet-sim/venv/bin/python tools/esporta_sim.py deaf --out test/test_sim_compare/sim_deaf.csv
```

Pacchetti (little-endian, packed, al massimo 250 byte): intestazione comune di 4 byte
`0x4D, versione del protocollo (2), tipo (1 beacon, 2 telemetria, 3 allarme, 4 comando), ID mittente`;
i pacchetti con intestazione sbagliata vengono scartati. Beacon: `seq u16, salti u8, t_s u16, stato u8,
n u8, [posizione 9], n x (id, segnale, pendenza, incertezza, eta')`. Il layout completo e'
documentato in `lib/core/packets.h`.

## Note sulle scelte

- **Pin**: tutti verificati sulle fonti Heltec, con riferimenti in `include/pins.h`. Vext
  (GPIO3) e' attivo alto sulla V1.1 e alimenta anche il display.
- **Nessun pin di carica/USB**: il TP4054 pilota solo il LED arancione, quindi il display non
  puo' mostrare `CARICA`.
- **Velocita'**: 802.11b 1 Mbit/s con `esp_now_set_peer_rate_config()`. Ogni scheda controlla
  su ogni pacchetto ricevuto che `sig_mode == 0` e `rate == 1M_L`; altrimenti scrive una riga
  `SYS velocita inattesa`.
- **Potenza**: `esp_wifi_set_max_tx_power(40)` (10 dBm), riletta con
  `esp_wifi_get_max_tx_power()` e scritta nell'intestazione.
- **Canale 1**: il simulatore usa il canale 6 (2,437 GHz); differenza di path loss circa 0,2 dB.
- **t_ms dei GPS** e' l'arrivo della frase NMEA, con precisione di pochi ms.
