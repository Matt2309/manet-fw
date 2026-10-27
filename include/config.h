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
constexpr int RX_QUEUE_LEN = 32;              // pacchetti ESP-NOW in attesa della logica
constexpr uint32_t HEARD_WINDOW_MS = 2000;    // un compagno e' "sentito" se l'ultimo beacon e' piu' recente
constexpr uint32_t LEADER_CONFLICT_MS = 2000; // un altro capogruppo e' "presente" se l'ultimo beacon e' piu' recente
// Dopo l'accensione una pressione lunga alterna corridore/capogruppo.
constexpr uint32_t ROLE_WINDOW_MS = 10000;
constexpr const char* ROLE_NVS_NAMESPACE = "manet";
constexpr const char* ROLE_NVS_KEY = "ruolo";

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

// --- Display e batteria ------------------------------------------------------
constexpr uint32_t DISPLAY_PERIOD_MS = 500;  // 2 Hz
constexpr int BAT_LOW_PCT = 20;
// Fattore di taratura della lettura batteria (da misurare con un multimetro;
// Meshtastic usa empiricamente 1,045).
constexpr float BAT_CAL = 1.018f;  // 2026-10-13: multimetro 3,89 V / display 3,82 V, scheda accesa
