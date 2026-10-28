// Formato dei byte del protocollo BLE nodo <-> app (docs/protocollo_ble.md di manet-mobile). Solo questo modulo lo conosce.
//
// Versione 1, little-endian. Dal nodo: STATO (0x01) e CONFERMA (0x03) sul canale dati, EVENTO (0x02) sul canale
// urgenti. Dall'app, sul canale comandi: START (0x10), STOP (0x11), CHIUDI_ALLARME (0x12), RIPRENDI_DA (0x13).
// Il seq (u32) esiste solo su STATO ed EVENTO, riparte da 1 a ogni START; prima dello START lo STATO ha seq 0 e t riservato.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ble {

constexpr uint8_t VERSION = 1;
constexpr size_t MAX_RUNNERS = 20;
constexpr size_t STATE_HEADER_SIZE = 11;
constexpr size_t ROW_SIZE = 6;
constexpr size_t EVENT_SIZE = 12;
constexpr size_t CONFIRM_SIZE = 4;
constexpr size_t CONFIRM_RESUME_SIZE = 8;
constexpr size_t MAX_MESSAGE_SIZE = STATE_HEADER_SIZE + MAX_RUNNERS * ROW_SIZE;  // 131
constexpr uint32_t T_NOT_STARTED = 0xFFFFFFFFu;
constexpr int8_t R_ABSENT = 0x7F;

enum class MessageType : uint8_t { State = 0x01, Event = 0x02, Confirm = 0x03 };
enum class CommandCode : uint8_t { Start = 0x10, Stop = 0x11, CloseAlarm = 0x12, ResumeFrom = 0x13 };
enum class RunnerState : uint8_t { Ok = 0, PreAlarm = 1, Lost = 2, Sos = 3 };
enum class EventType : uint8_t { PreAlarm = 0, Lost = 1, Sos = 2, Return = 3 };
enum class AlarmKind : uint8_t { PreAlarm = 0, Lost = 1, Sos = 2 };  // tipo di allarme in CHIUDI_ALLARME

struct Row {
    uint8_t id;
    RunnerState state;
    int8_t r;           // dBm, R_ABSENT se non c'e' una stima valida
    uint8_t battery;    // 0..100
    uint16_t age_s;     // secondi dall'ultimo dato
};

struct Command {
    CommandCode code;
    uint8_t id;          // CHIUDI_ALLARME
    AlarmKind kind;      // CHIUDI_ALLARME
    uint32_t last_seq;   // RIPRENDI_DA
};

// Byte scritti in out; 0 se cap non basta o i campi non rispettano le regole del formato
// (piu' di MAX_RUNNERS righe, seq 0 con t valido, seq diverso da 0 con t riservato).
size_t encode_state(uint32_t seq, uint32_t t, const Row* rows, size_t n, uint8_t* out, size_t cap);
size_t encode_event(uint32_t seq, uint32_t t, uint8_t runner, EventType type, uint8_t* out, size_t cap);
// CONFERMA di un comando: 4 byte; per RIPRENDI_DA usare encode_confirm_resume.
size_t encode_confirm(CommandCode cmd, bool ok, uint8_t* out, size_t cap);
size_t encode_confirm_resume(bool ok, uint32_t last_seq, uint8_t* out, size_t cap);

// false se versione, lunghezza o codici non tornano.
bool decode_command(const uint8_t* data, size_t len, Command& c);

// seq di uno STATO o EVENTO gia' codificato (serve a rileggerli dal file); false se non lo e'.
bool message_seq(const uint8_t* msg, size_t len, uint32_t& seq);

}  // namespace ble
