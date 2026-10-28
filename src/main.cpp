// manet-fw — firmware per la Heltec Wireless Tracker V1.1.
//
// Trasmette beacon ESP-NOW come nel simulatore (Blocco 3) e registra tutto su LittleFS
// per l'analisi offline. La logica sta in lib/core; qui si avviano i task.

#include <Arduino.h>
#include <esp_mac.h>
#include <stdarg.h>
#include <stdio.h>

#include "app.h"
#include "config.h"
#include "console.h"
#include "gnss.h"
#include "identity.h"
#include "logger.h"
#include "logic.h"
#include "lora_sleep.h"
#include "nodes.h"
#include "pins.h"
#include "radio.h"
#include "ui.h"

NodeInfo g_node = {};
SemaphoreHandle_t g_serial_mutex = nullptr;

void dbg(const char* fmt, ...) {
    char buf[200];
    int n = snprintf(buf, sizeof(buf), "# ");
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
    va_end(ap);
    if (g_serial_mutex != nullptr && xSemaphoreTake(g_serial_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        Serial.println(buf);
        xSemaphoreGive(g_serial_mutex);
    }
}

void setup() {
    g_serial_mutex = xSemaphoreCreateMutex();
    Serial.begin(115200);
    Serial.setTxTimeoutMs(100);

    // Vext alimenta GNSS e display (attivo alto sulla V1.1).
    pinMode(pins::vext, OUTPUT);
    digitalWrite(pins::vext, pins::vext_on ? HIGH : LOW);
    delay(100);

    // Identita' dal MAC.
    esp_read_mac(g_node.mac, ESP_MAC_WIFI_STA);
    snprintf(g_node.mac_str, sizeof(g_node.mac_str), "%02X:%02X:%02X:%02X:%02X:%02X", g_node.mac[0],
             g_node.mac[1], g_node.mac[2], g_node.mac[3], g_node.mac[4], g_node.mac[5]);
    identity::Id who = identity::load(g_node.mac);
    g_node.id = who.id;
    g_node.id_source = identity::source_name(who.source);
    dbg("manet-fw %s id=%d (%s) mac=%s", FW_VERSION, g_node.id, g_node.id_source, g_node.mac_str);
    if (g_node.id == 0) dbg("ID non assegnato: dalla seriale, comando 'id N' (1..%d)", MAX_NODES);

    // La radio LoRa non serve.
    char lora_msg[64];
    bool lora_ok = lora_sleep(lora_msg, sizeof(lora_msg));
    dbg("%s", lora_msg);

    // Coda e file system, poi la radio Wi-Fi (serve la potenza riletta per l'intestazione).
    bool fs_ok = logger::init();
    radio::Info rinfo;
    bool radio_ok = radio::init(rinfo);

    Role role = logic_init();
    const char* rate = rinfo.rate_ok ? "11b_1M" : "ERRORE";
    logger::set_header_info(g_node.id, g_node.mac_str, rinfo.power_qdbm, rate, role_name(role));
    if (fs_ok && logger::open_new()) {
        dbg("registrazione su %s", logger::current_path());
        logger::post_sys("%s", lora_msg);
        if (!radio_ok) logger::post_sys("radio errore di inizializzazione");
        if (!rinfo.power_ok) {
            logger::post_sys("potenza diversa: impostata %d riletta %d", TX_POWER_QDBM,
                             rinfo.power_qdbm);
        }
        if (!rinfo.rate_ok) logger::post_sys("velocita non applicata err=%d", rinfo.rate_err);
        logger::post_sys("id %d origine %s", g_node.id, g_node.id_source);
    } else {
        dbg("ERRORE: file system o apertura del file falliti");
    }
    (void)lora_ok;

    logger::start_task();
    ui_start();
    gnss_start();
    if (radio_ok) {
        radio::start();
        logic_start();
    }
}

void loop() {
    console_poll();
    delay(5);
}
