// Pacchetti ESP-NOW: strutture packed, little-endian, al massimo 250 byte.
//
// Intestazione comune (4 byte):  magic | versione protocollo | tipo | id mittente
//
//   Beacon      seq u16 | salti u8 | t_s u16 | stato u8 | n u8 | [pos 9] | n x voce(5)
//               voce = id | segnale i8 (1 dB) | pendenza i8 (0,1 dB/s) | incertezza u8 (0,1 dB) | eta' u8 (0,1 s)
//               stato = bit 0-3 batteria (0..15), bit 4 corsa ferma, bit 5 SOS, bit 6 posizione presente
//   Telemetria  n record u8 | record...
//               record = id | t_s u16 | flag u8 | n u8 | [pos 9] | n x voce(5)
//               voce = id | segnale fuso i8 | incertezza u8 | eta' u8 | allarme u8
//               flag = bit 0-3 batteria, bit 4 SOS, bit 5 isolato, bit 6 posizione presente
//   Allarme     seq u8 | id interessato | tipo | t_s ultimo contatto u16 | ultimo segnale i8 | [pos 9]
//   Comando     seq u8 | comando | decimi di secondo trascorsi u16
//   Posizione   lat i32 (1e-7 gradi) | lon i32 (1e-7 gradi) | HDOP u8 (0,1)
//
// I rilanci di allarmi e comandi lasciano l'intestazione com'e': l'id del mittente
// resta quello di chi ha creato il pacchetto.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "config.h"

namespace packets {

constexpr uint8_t MAGIC = 0x4D;
constexpr size_t HEADER_SIZE = 4;
constexpr size_t POS_SIZE = 9;
constexpr size_t ENTRY_SIZE = 5;
constexpr size_t MAX_NEIGHBORS = MAX_NODES - 1;
constexpr size_t MAX_SIZE = ESPNOW_MAX_PAYLOAD;

enum class Type : uint8_t { Beacon = 1, Telemetry = 2, Alarm = 3, Command = 4 };

constexpr uint16_t T_NOT_STARTED = 0xFFFF;  // secondi da T0: corsa non avviata
constexpr uint8_t HOPS_ISOLATED = 255;
constexpr uint8_t SIGMA_NA = 255;

// Quantizzazione dei campi.
constexpr float SLOPE_STEP = 0.1f;  // dB/s
constexpr float SIGMA_STEP = 0.1f;  // dB
constexpr uint8_t BATTERY_MAX = 15;

struct Position {
    int32_t lat_e7;
    int32_t lon_e7;
    uint8_t hdop_dm;
};

// --- Intestazione ------------------------------------------------------------
// true se magic e versione tornano e il tipo e' noto; riempie type e sender.
bool read_header(const uint8_t* data, size_t len, Type& type, uint8_t& sender);

// --- Beacon ------------------------------------------------------------------
struct BeaconEntry {
    uint8_t id;
    int8_t signal;
    int8_t slope;
    uint8_t sigma;
    uint8_t age_ds;  // saturata a 255
};

struct Beacon {
    uint8_t sender;
    uint16_t seq;
    uint8_t hops;
    uint16_t t_s;
    uint8_t battery;  // 0..BATTERY_MAX
    bool stopped;
    bool sos;
    bool has_pos;
    Position pos;
    uint8_t n;
    BeaconEntry nb[MAX_NEIGHBORS];
};

size_t beacon_size(uint8_t n, bool has_pos);
// Byte "stato" del beacon: batteria, corsa ferma, SOS, posizione presente.
uint8_t beacon_status(const Beacon& b);
// Byte scritti; 0 se cap non basta o n e' fuori limite.
size_t pack_beacon(const Beacon& b, uint8_t* out, size_t cap);
// false se intestazione, tipo o lunghezza non tornano.
bool unpack_beacon(const uint8_t* data, size_t len, Beacon& b);

// --- Telemetria --------------------------------------------------------------
struct TelEntry {
    uint8_t id;
    int8_t signal;
    uint8_t sigma;
    uint8_t age_ds;
    uint8_t alarm;
};

struct TelRecord {
    uint8_t id;
    uint16_t t_s;
    uint8_t battery;
    bool sos;
    bool isolated;
    bool has_pos;
    Position pos;
    uint8_t n;
    TelEntry nb[MAX_NEIGHBORS];
};

size_t telemetry_record_size(uint8_t n, bool has_pos);
// Mette nel pacchetto quanti piu' record di recs[0..count) ci stanno, nell'ordine;
// packed e' il loro numero. Restituisce i byte scritti, 0 se nemmeno il primo ci sta.
size_t pack_telemetry(uint8_t sender, const TelRecord* recs, size_t count, uint8_t* out, size_t cap,
                      size_t& packed);
// Legge fino a max record; count e' quanti ne ha letti. false se il pacchetto e' malformato
// o contiene piu' di max record.
bool unpack_telemetry(const uint8_t* data, size_t len, uint8_t& sender, TelRecord* recs, size_t max,
                      size_t& count);

// --- Allarme -----------------------------------------------------------------
enum class AlarmType : uint8_t { PreAlarm = 1, Lost = 2, Sos = 3, End = 4 };

struct Alarm {
    uint8_t sender;  // chi ha creato l'allarme
    uint8_t seq;
    uint8_t runner;  // corridore interessato
    AlarmType type;
    uint16_t last_t_s;
    int8_t last_signal;
    bool has_pos;  // posizione di chi segnala
    Position pos;
};

size_t pack_alarm(const Alarm& a, uint8_t* out, size_t cap);
bool unpack_alarm(const uint8_t* data, size_t len, Alarm& a);

// --- Comando -----------------------------------------------------------------
enum class Cmd : uint8_t { Start = 1, Stop = 2 };

struct Command {
    uint8_t sender;  // chi ha creato il comando
    uint8_t seq;
    Cmd cmd;
    uint16_t elapsed_ds;  // decimi di secondo dall'invio originale
};

size_t pack_command(const Command& c, uint8_t* out, size_t cap);
bool unpack_command(const uint8_t* data, size_t len, Command& c);

// --- Quantizzazione ----------------------------------------------------------
uint8_t age_ds(uint32_t elapsed_ms);  // decimi di secondo, saturata a 255
int8_t quantize_i8(float v, float step);   // arrotonda e satura
uint8_t quantize_u8(float v, float step);  // arrotonda e satura a 0..254 (255 = n.d.)
uint8_t battery_code(int percent);         // 0..100 -> 0..BATTERY_MAX
int battery_percent(uint8_t code);
int32_t degrees_to_e7(double deg);

}  // namespace packets
