// Registrazione su LittleFS.
//
// I callback e i task non scrivono su flash: pubblicano eventi in una coda
// FreeRTOS; un task separato li formatta in un buffer in RAM e ogni 5 s scrive
// e sincronizza il file.
#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "csv.h"
#include "packets.h"
#include "nmea.h"

enum class LogEventType : uint8_t { Tx, Rx, Gps, Evt, Bat, Sys, Kal, Det, Tel, Cmd, Ble, Alm };

struct LogEvent {
    LogEventType type;
    uint32_t t_ms;
    union {
        struct {
            uint32_t seq;
        } tx;
        struct {
            int8_t rssi;
            packets::Beacon b;
        } rx;
        GpsEpoch gps;
        struct {
            int n;
        } evt;
        struct {
            int mv;
        } bat;
        char sys[96];
        struct {
            uint8_t id;
            float signal, slope, sigma;
            uint8_t age_ds;
        } kal;
        struct {
            DetVersion version;
            DetKind kind;
            bool start;
            uint8_t id;
            float r;
            float threshold;
        } det;
        struct {
            TelAction action;
            uint8_t id;
            uint16_t t_s;
            uint8_t peer;
            uint16_t queued;
            uint8_t tries;
            int32_t ms;
        } tel;
        struct {
            AlmAction action;
            uint8_t creator;
            uint8_t seq;
            uint8_t runner;
            packets::AlarmType type;
        } alm;
        struct {
            packets::Cmd cmd;
            CmdOrigin origin;
            uint8_t seq;
            uint16_t elapsed_ds;
        } cmd;
        struct {
            BleEvent event;
            int detail;
        } ble;
    } u;
};

namespace logger {

enum class State : uint8_t {
    Off,        // nessun file aperto (avvio, o dopo "format")
    Recording,
    Saved,      // chiuso con pressione lunga o con "get"
    Full,       // spazio sotto il 5%
    Error,      // montaggio o apertura falliti
};

// Crea la coda e monta il file system (formatta se il montaggio fallisce).
bool init();

// Dati per l'intestazione del file.
void set_header_info(int id, const char* mac, int power_qdbm, const char* rate, const char* role);

// Cambio di ruolo (nella finestra dopo l'accensione): riscrive il ruolo nell'intestazione del
// file aperto e nei file che si apriranno. role ha sempre 4 caratteri ("CORR" o "CAPO").
void set_role(const char* role);

// Apre il primo /run_NNN.csv libero e scrive l'intestazione. State -> Recording.
bool open_new();

void start_task();

// Pubblica un evento; thread-safe, usabile dai callback. Se la coda e' piena
// l'evento e' perso e contato.
void post(const LogEvent& e);
void post_sys(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// Flush e chiusura del file aperto, con motivo nella riga SYS. State -> Saved.
void save_and_close(const char* reason);

State state();
// Nome del file corrente senza '/' e senza estensione ("run_004"); "" se nessuno.
const char* current_name();
// Percorso del file corrente ("/run_004.csv"); "" se nessuno.
const char* current_path();

// Spazio del file system.
size_t fs_total();
size_t fs_used();

// Blocco del file system, per la console (ls, get, rm, format).
void lock();
void unlock();
// Riformatta il file system: da chiamare con il lock preso e nessun file aperto.
bool format_locked();

}  // namespace logger
