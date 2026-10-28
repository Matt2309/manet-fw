# manet-fw

Firmware per la **Heltec Wireless Tracker V1.1** (ESP32-S3FN8). Le schede trasmettono beacon
ESP-NOW come il Blocco 3 di `manet-sim` e registrano tutto su LittleFS; l'analisi si fa dopo,
offline. La logica (pacchetti, vicini, ruolo) e' C++ puro in `lib/core`, testata con
`pio test -e native`; in `src/` restano gli adattatori verso l'hardware. Il firmware e' in
costruzione per fasi (rilevatore, GNSS, percorso dei dati, BLE): per ora fa quello che si e' detto sopra.

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
| pressione breve (< 1 s) | riga `EVT` numerata, il LED lampeggia |
| pressione lunga (3 s) iniziata nei primi 10 s dopo l'accensione | alterna `CORR`/`CAPO` e lo salva in NVS |
| pressione lunga (3 s), dopo | flush e chiusura, display `SALVATO`; un'altra pressione lunga apre un nuovo file |

Il ruolo resta quello salvato anche dopo un riavvio (alla prima accensione e' `CORR`) e si cambia
solo prima dello START. Non tenere premuto il tasto durante l'accensione: GPIO0 e' il pin di avvio.
La finestra conta dall'inizio della pressione: contano anche i 3 s della pressione, che puo' scattare dopo i 10 s.
Sul display la riga 3 mostra, nella finestra, `premi 3s=ruolo (Ns)` con i secondi che restano; poi i salti dal
capogruppo e i compagni sentiti; un capogruppo che sente un altro capogruppo mostra `DUE CAPOGRUPPO`.

Da fermo i beacon continuano a partire, cosi' le altre schede non vedono perdite fittizie.
Sotto il 5% di spazio libero la scheda smette di scrivere e mostra `MEMORIA PIENA`
(circa 5 ore di registrazione). Con un distacco della batteria si perdono al massimo 5 s.

## Scaricare i dati

```
python tools/scarica.py                 # tutte le schede collegate
python tools/scarica.py --porta /dev/cu.usbmodemXXXX
```

Salva in `dati/<data>/scheda_<id>/` controllando dimensione e CRC32; non cancella nulla.
**`get` sul file aperto lo chiude**: la registrazione di quella scheda si ferma.

Comandi seriali (115200, uno per riga):

| Comando | Risposta |
|---|---|
| `info` | `INFO id= mac= fw= file= state=` |
| `ls` | una riga `FILE nome dimensione crc32` per file, poi `END` |
| `free` | `FREE total= used= free=` |
| `get <file>` | `BEGIN nome dimensione crc32`, i byte del file, `END` |
| `rm <file>` | `OK`; rifiuta il file aperto |
| `format SI` | formatta e ferma la registrazione |

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
KAL,t_ms,id,segnale,pendenza,incertezza,eta                stime del Kalman (dalla fase 2)
DET,t_ms,versione,tipo,inizio|fine,id,R                    decisioni del rilevatore (dalla fase 2)
TEL,t_ms,azione,id,t_s,compagno,n_coda,tentativi           telemetria (dalla fase 4)
CMD,t_ms,comando,origine,seq,decimi                        START e STOP (dalla fase 4)
BLE,t_ms,evento,dettaglio                                  connessioni e cadute, solo capogruppo (fase 5)
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
