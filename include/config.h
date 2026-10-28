// Parametri del firmware, in un unico punto.
#pragma once

#include <stdint.h>

#define FW_VERSION "2.0.0"

// --- Protocollo --------------------------------------------------------------
// Versione scritta nell'intestazione di ogni pacchetto: le schede con un firmware
// diverso scartano i pacchetti a vicenda.
constexpr uint8_t PROTO_VERSION = 2;
// Corridori al massimo: ESP-NOW ammette 20 peer in tutto (19 compagni + broadcast).
constexpr int MAX_NODES = 20;
constexpr int ESPNOW_MAX_PAYLOAD = 250;  // ESP-NOW v1

// --- Radio (allineata a manet-sim, config/default.yaml) ----------------------
// Canale Wi-Fi fisso. Nota: il simulatore usa 2,437 GHz (canale 6).
constexpr uint8_t RADIO_CHANNEL = 1;
// Potenza in quarti di dBm, come esp_wifi_set_max_tx_power(): 40 = 10 dBm.
constexpr int8_t TX_POWER_QDBM = 40;
constexpr uint32_t BEACON_PERIOD_MS = 500;
constexpr uint32_t BEACON_JITTER_MS = 50;  // uniforme in [-J, +J]

// --- Logica e ruolo ----------------------------------------------------------
constexpr uint32_t LOGIC_TICK_MS = 100;       // controlli a tempo
constexpr uint32_t LOGIC_MAX_WAIT_MS = 20;    // la logica si risveglia almeno cosi' spesso (rilanci con ritardo breve)
constexpr int RX_QUEUE_LEN = 32;              // pacchetti ESP-NOW in attesa della logica
constexpr uint32_t HEARD_WINDOW_MS = 2000;    // un compagno e' "sentito" se l'ultimo beacon e' piu' recente
constexpr uint32_t LEADER_CONFLICT_MS = 2000; // un altro capogruppo e' "presente" se l'ultimo beacon e' piu' recente
// Dopo l'accensione una pressione lunga alterna corridore/capogruppo.
constexpr uint32_t ROLE_WINDOW_MS = 10000;
constexpr const char* ROLE_NVS_NAMESPACE = "manet";
constexpr const char* ROLE_NVS_KEY = "ruolo";

// --- Rilevatore (lo stesso di manet-sim, ritarato sul canale misurato) -------
// Kalman per compagno: stato [segnale, pendenza].
constexpr double KALMAN_SIGMA_A = 0.25;            // dB/s^2, rumore di processo
constexpr double KALMAN_R = 10.84;                 // dB^2, rumore di misura
constexpr double KALMAN_INITIAL_SLOPE_STD = 3.0;   // dB/s, incertezza iniziale sulla pendenza
constexpr uint32_t KALMAN_REINIT_GAP_MS = 10000;   // dopo un silenzio piu' lungo il filtro riparte da zero
// Fusione: le voci piu' vecchie di questo valore sono escluse.
constexpr uint32_t FUSION_MAX_AGE_MS = 2000;
// Pre-allarme: segnale fuso sotto soglia per almeno PRE_ALARM_MIN_MS.
constexpr double PRE_ALARM_DBM = -93.0;
constexpr uint32_t PRE_ALARM_MIN_MS = 1000;
// Spegnimento: segnale sopra soglia + margine per RELEASE_TIME_MS, con almeno RELEASE_RECEPTION
// dei beacon attesi ricevuti in quel tempo (il nodo perso si spegne solo con questa ricezione).
constexpr double RELEASE_MARGIN_DB = 3.0;
constexpr uint32_t RELEASE_TIME_MS = 30000;
constexpr double RELEASE_RECEPTION = 0.8;
// Nodo perso: nessun beacon da questo tempo.
constexpr uint32_t LOST_SILENCE_MS = 10000;
constexpr uint32_t KAL_LOG_PERIOD_MS = 1000;       // righe KAL nel registro
// --- GNSS ---------------------------------------------------------------------
// Fix valido: 3D, almeno GNSS_MIN_SAT satelliti, HDOP al massimo GNSS_MAX_HDOP, epoca piu' giovane di GNSS_MAX_AGE_MS.
constexpr int GNSS_MIN_SAT = 6;
constexpr double GNSS_MAX_HDOP = 2.5;
constexpr uint32_t GNSS_MAX_AGE_MS = 2000;
// Ramo GNSS del pre-allarme: con fix valido su entrambe le schede si confrontano distanza e segnale fuso
// di GNSS_WINDOW_MS fa (un campione ogni GNSS_SAMPLE_MS).
constexpr uint32_t GNSS_WINDOW_MS = 10000;
constexpr uint32_t GNSS_SAMPLE_MS = 1000;
constexpr double GNSS_GROW_M = 8.0;     // la distanza "cresce" se aumenta di piu' di cosi'
constexpr double GNSS_STILL_M = 3.0;    // la distanza "resta ferma" se varia al massimo di cosi' (in piu' o in meno)
constexpr double GNSS_R_DROP_DB = 3.0;  // il segnale "scende" se cala di almeno cosi'
constexpr double GNSS_THRESHOLD_GROW_DBM = -90.0;   // distanza che cresce e segnale che scende
constexpr double GNSS_THRESHOLD_STILL_DBM = -95.0;  // distanza ferma e segnale che scende, con conferma piu' lunga
constexpr uint32_t GNSS_STILL_MIN_MS = 3000;
// Quale versione del rilevatore conta per gli allarmi (entrambe si registrano).
constexpr bool ALARM_USES_GNSS = false;
constexpr int GNSS_HISTORY_LEN = GNSS_WINDOW_MS / GNSS_SAMPLE_MS + 2;

// Ricezioni da ricordare per compagno: quelle della finestra di spegnimento al periodo minimo.
constexpr int RX_HISTORY_LEN = RELEASE_TIME_MS / (BEACON_PERIOD_MS - BEACON_JITTER_MS) + 2;
// Beacon minimi nella finestra di spegnimento (arrotondati per eccesso).
constexpr int RELEASE_MIN_BEACONS =
    static_cast<int>(RELEASE_RECEPTION * RELEASE_TIME_MS / BEACON_PERIOD_MS - 1e-9) + 1;

// --- Corsa e percorso dei dati verso il capogruppo -----------------------------
// Due T0 stimati che differiscono meno di cosi' sono la stessa corsa (i secondi da T0 nei beacon sono interi).
constexpr uint32_t RUN_T0_TOLERANCE_MS = 3000;
// Compagno "sentito bene" e candidato per l'invio: stima del segnale sopra questo valore, sentito negli ultimi HEARD_WINDOW_MS.
constexpr double ROUTE_MIN_DBM = -88.0;
// Telemetria: un record proprio al secondo; la coda tiene 10 minuti.
constexpr uint32_t TEL_PERIOD_MS = 1000;
constexpr int TEL_QUEUE_LEN = 600;
constexpr uint32_t TEL_DEGRADED_PERIOD_MS = 5000;  // a coda piena un record proprio ogni cosi'
constexpr int TEL_ATTEMPTS = 3;                    // tentativi su un candidato prima del successivo
constexpr uint32_t TEL_MIN_GAP_MS = 100;           // fra due invii di telemetria
constexpr uint32_t TEL_RESULT_TIMEOUT_MS = 1000;   // conferma ESP-NOW mancante = tentativo fallito
constexpr uint32_t TEL_QUEUE_LOG_MS = 5000;        // riga TEL "coda" quando non ci sono candidati
constexpr uint32_t TEL_RETRY_MS = 1000;            // dopo aver provato tutti i candidati si riscegliera' a questo intervallo
constexpr int SEEN_RECORD_WINDOW = 1024;           // secondi da T0 ricordati per mittente, contro i doppioni
constexpr uint32_t SOS_PERIOD_MS = 10000;          // l'SOS si ripete a questo intervallo
constexpr uint32_t RELAY_JITTER_MS = 40;           // ritardo casuale (0..J) del rilancio di allarmi e comandi
constexpr uint32_t DISPLAY_ON_MS = 10000;          // schermo acceso dopo un tasto o un avviso
constexpr uint32_t HEAP_LOG_PERIOD_MS = 60000;     // riga SYS con l'heap libero
// --- Capogruppo: tabella del gruppo -----------------------------------------------------------------------
constexpr uint32_t R_REPORT_WINDOW_MS = 5000;     // R di un corridore = media dei segnali fusi riportati in questa finestra
constexpr uint32_t OBSERVER_VALID_MS = 30000;     // l'allarme riportato da un osservatore silenzioso per tanto smette di contare
constexpr uint32_t SOS_HOLD_MS = SOS_PERIOD_MS * 5 / 2;  // un SOS e' "in corso" finche' l'ultima indicazione e' piu' recente di cosi'
constexpr uint32_t RECORD_FRESH_S = 5;            // un record piu' vecchio non descrive lo stato di adesso
constexpr int PRE_MIN_OBSERVERS = 1;              // osservatori per il pre-allarme
constexpr int LOST_MIN_OBSERVERS = 2;             // osservatori per il nodo perso, al massimo: non piu' di quante schede vedono il corridore
constexpr uint32_t STATE_PERIOD_MS = 5000;        // STATO sul BLE

// --- BLE (solo capogruppo) -----------------------------------------------------------------------------------
// UUID scelti per questo progetto: l'app li deve usare uguali (vedi docs/protocollo_ble.md dell'app).
constexpr const char* BLE_SERVICE_UUID = "a1f0c0de-4d41-4e45-8001-6d616e657431";
constexpr const char* BLE_DATA_UUID = "a1f0c0de-4d41-4e45-8002-6d616e657431";      // notify: STATO, CONFERMA
constexpr const char* BLE_URGENT_UUID = "a1f0c0de-4d41-4e45-8003-6d616e657431";    // notify: EVENTO
constexpr const char* BLE_COMMAND_UUID = "a1f0c0de-4d41-4e45-8004-6d616e657431";   // write: comandi
constexpr uint16_t BLE_MTU = 247;
// Intervallo di connessione lungo (basta uno STATO ogni 5 s): unita' di 1,25 ms e di 10 ms. Valori da confermare con la prova F1.
constexpr uint16_t BLE_CONN_INTERVAL_MIN = 400;   // 500 ms
constexpr uint16_t BLE_CONN_INTERVAL_MAX = 800;   // 1000 ms
constexpr uint16_t BLE_CONN_LATENCY = 0;
constexpr uint16_t BLE_CONN_TIMEOUT = 600;        // 6 s
constexpr uint32_t BLE_NOTIFY_GAP_MS = 20;        // pausa fra due notify (rinvio dopo RIPRENDI_DA)
constexpr const char* RUN_NVS_ACTIVE_KEY = "corsa_att";  // bandiera NVS: corsa in corso, per riprendere dopo un riavvio
constexpr const char* RUN_NVS_COUNT_KEY = "corsa_n";     // contatore dei file della corsa

// Prova F2 (opzione di build tracker_f2): ogni F2_PERIOD_MS un unicast con conferma da circa 223 byte a un compagno a rotazione.
#ifdef MANET_F2
constexpr bool F2_TEST = true;
#else
constexpr bool F2_TEST = false;
#endif
constexpr uint32_t F2_PERIOD_MS = 1000;
constexpr int F2_RECORDS = 2;                      // record da 19 vicini e posizione: 109 byte ciascuno

// --- Registrazione -----------------------------------------------------------
constexpr uint32_t FLUSH_INTERVAL_MS = 5000;
constexpr uint32_t BAT_LOG_INTERVAL_MS = 30000;
constexpr uint32_t SPACE_CHECK_INTERVAL_MS = 5000;
constexpr int FREE_SPACE_MIN_PCT = 5;
constexpr int EVENT_QUEUE_LEN = 256;
constexpr int LOG_BUFFER_BYTES = 16384;
constexpr int LOG_LINE_BYTES = 512;  // una riga RX con 19 vicini ne occupa circa 450
// true: le righe RX dei beacon ricevuti perdono la tabella dei vicini e le colonne
// aggiunte (per i gruppi grandi, dove il registro si riempirebbe presto).
constexpr bool LOG_RX_REDUCED = false;

// --- Tasto USER --------------------------------------------------------------
constexpr uint32_t BTN_DEBOUNCE_MS = 30;
constexpr uint32_t BTN_SHORT_MAX_MS = 1000;
constexpr uint32_t BTN_LONG_MS = 3000;
constexpr uint32_t BTN_POLL_MS = 10;
constexpr uint32_t LED_BLINK_MS = 100;
constexpr uint32_t SOS_LED_HALF_PERIOD_MS = 250;  // il LED lampeggia a questo ritmo finche' l'SOS e' attivo

// --- Display e batteria ------------------------------------------------------
constexpr uint32_t DISPLAY_PERIOD_MS = 500;  // 2 Hz
constexpr int BAT_LOW_PCT = 20;
// Fattore di taratura della lettura batteria (da misurare con un multimetro;
// Meshtastic usa empiricamente 1,045).
constexpr float BAT_CAL = 1.018f;  // 2026-10-13: multimetro 3,89 V / display 3,82 V, scheda accesa
