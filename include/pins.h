// Pin della Heltec Wireless Tracker V1.1 (ESP32-S3FN8).
//
// Tutti i valori sono stati verificati sulle fonti ufficiali Heltec e
// coincidono con i valori indicativi della specifica.
//
// Fonti:
//  [S1] Schema V1.1 (HTIT-Tracker_V0.5, 2023-07-31):
//       https://resource.heltec.cn/download/Wireless_Tracker/Wireless_Tacker1.1/HTIT-Tracker_V0.5.pdf
//  [S2] Esempi GPS Heltec: github.com/HelTecAutomation/Heltec_ESP32
//       examples/GPS/GPSToUart/GPSToUart.ino (righe 8, 13-15)
//  [S3] Driver TFT Heltec: Heltec_ESP32/src/HT_st7735.h e HT_st7735.cpp
//  [S4] Registro revisioni hardware:
//       https://docs.heltec.cn/en/node/esp32/hardware_update_log.html
//  [S6] Controllo incrociato: Meshtastic, variants/esp32s3/heltec_wireless_tracker/variant.h
//
// Attenzione: la variante "heltec_wireless_tracker" di arduino-esp32 dichiara
// LED_BUILTIN=48 (LED RGB di un'altra scheda) e SDA/SCL=41/42 (in conflitto con
// il TFT). Qui non si usano: valgono solo i pin sotto.
#pragma once

namespace pins {

// Vext: LDO che alimenta GNSS, LNA del GNSS e TFT (logica e retroilluminazione).
// Attivo ALTO sulla V1.1 (sulla V1.0 era attivo basso). [S1] [S2] [S4] [S6]
constexpr int vext = 3;
constexpr int vext_on = 1;

// Tasto USER: attivo basso, pull-up esterno da 10k. E' anche il pin di strap
// per l'avvio (GPIO0): si legge solo a scheda accesa. [S1]
constexpr int button = 0;

// LED bianco, attivo alto. [S1]
constexpr int led = 18;

// UART del GNSS UC6580, 115200 8N1. Numeri dal punto di vista dell'ESP32:
// RX = 33 (riceve dal GNSS), TX = 34. [S2] riga 15: Serial1.begin(115200,8N1,33,34)
constexpr int gnss_rx = 33;
constexpr int gnss_tx = 34;
constexpr long gnss_baud = 115200;

// TFT ST7735S 160x80. Nessun MISO. [S3] [S6]
constexpr int tft_cs = 38;
constexpr int tft_rst = 39;
constexpr int tft_dc = 40;
constexpr int tft_sclk = 41;
constexpr int tft_mosi = 42;
// Retroilluminazione: GPIO21 pilota un MOSFET sul catodo, ALTO = acceso. [S1] [S3]
constexpr int tft_backlight = 21;

// Batteria: partitore 390k/100k (x4,9) su GPIO1, abilitato da GPIO2
// (ALTO = partitore collegato, solo sulla V1.1). [S1] [S6]
constexpr int vbat_adc = 1;
constexpr int vbat_enable = 2;
constexpr float vbat_divider = 4.9f;

// Radio SX1262: non usata, viene messa in sleep all'avvio. [S1] [S6]
constexpr int lora_nss = 8;
constexpr int lora_sck = 9;
constexpr int lora_mosi = 10;
constexpr int lora_miso = 11;
constexpr int lora_rst = 12;
constexpr int lora_busy = 13;

// Non esiste alcun pin che segnali USB collegato o batteria in carica: il
// TP4054 pilota solo il LED arancione di carica. [S1] Quindi niente "CARICA".

}  // namespace pins
