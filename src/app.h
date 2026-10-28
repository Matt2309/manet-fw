// Stato condiviso tra i moduli del firmware.
#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Identita' della scheda, fissata all'avvio.
struct NodeInfo {
    uint8_t id;           // 1..MAX_NODES, 0 se il MAC non e' in tabella
    uint8_t mac[6];
    char mac_str[18];     // "AA:BB:CC:DD:EE:FF"
};
extern NodeInfo g_node;

// Stato del GNSS per il display.
struct GpsStatus {
    volatile bool seen;       // almeno un'epoca ricevuta
    volatile uint32_t last_ms;
    volatile int fix;         // 0, 2, 3
    volatile bool valid;      // fix valido (3D, satelliti, HDOP): vedi fix_valid()
    volatile int sat;
};
extern GpsStatus g_gps;

// Carica della batteria in %, aggiornata dal task dell'interfaccia (media mobile).
extern volatile int g_battery_pct;

// Mutex dell'uscita seriale: i messaggi di debug (righe che iniziano con '#')
// non devono mescolarsi alle risposte e ai trasferimenti binari.
extern SemaphoreHandle_t g_serial_mutex;

// Messaggio di debug sulla seriale, come riga "# ...".
void dbg(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
