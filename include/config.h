// Parametri del firmware, in un unico punto.
#pragma once

#include <stdint.h>

#define FW_VERSION "1.0.0"

// --- Radio (allineata a manet-sim, config/default.yaml) ----------------------
// Canale Wi-Fi fisso. Nota: il simulatore usa 2,437 GHz (canale 6).
constexpr uint8_t RADIO_CHANNEL = 1;
// Potenza in quarti di dBm, come esp_wifi_set_max_tx_power(): 40 = 10 dBm.
constexpr int8_t TX_POWER_QDBM = 40;
constexpr uint32_t BEACON_PERIOD_MS = 500;
constexpr uint32_t BEACON_JITTER_MS = 50;  // uniforme in [-J, +J]

// --- Registrazione -----------------------------------------------------------
constexpr uint32_t FLUSH_INTERVAL_MS = 5000;
constexpr uint32_t BAT_LOG_INTERVAL_MS = 30000;
constexpr uint32_t SPACE_CHECK_INTERVAL_MS = 5000;
constexpr int FREE_SPACE_MIN_PCT = 5;
constexpr int EVENT_QUEUE_LEN = 256;
constexpr int LOG_BUFFER_BYTES = 16384;

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
