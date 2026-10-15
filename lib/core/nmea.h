// Parser NMEA per il GNSS UC6580 (talker GN, GP, GB, GL, GA, ...).
//
// Le frasi RMC e GGA (che portano l'ora UTC) delimitano le epoche: quando arriva
// una frase con un'ora diversa, l'epoca precedente e' completa e viene restituita.
// GSA, senza ora, si attacca all'epoca in corso. t_ms e' l'istante di arrivo della
// prima frase dell'epoca.
//
// Da RMC: data, ora, velocita', rotta. Da GGA: posizione, quota, HDOP, satelliti,
// qualita'. Da GSA: modo del fix (1 = nessuno, 2 = 2D, 3 = 3D); con piu' costellazioni
// si prende il massimo.
#pragma once

#include <stddef.h>
#include <stdint.h>

// Epoca GNSS. Struttura semplice, senza inizializzatori, per poterla mettere in una union.
struct GpsEpoch {
    uint32_t t_ms;
    bool has_time;
    bool has_date;
    int year, month, day;
    int hour, minute, second, centisecond;
    bool has_pos;
    double lat, lon;
    bool has_alt;
    float alt;
    bool has_speed;
    float speed_kmh;
    bool has_course;
    float course;
    bool has_hdop;
    float hdop;
    bool has_sat;
    int sat;
    int fix;  // 0 = nessun fix, 2 = 2D, 3 = 3D
};

// true se la riga NMEA (da '$' a '*' escluso) ha il checksum giusto dopo '*'.
bool nmea_checksum_ok(const char* sentence);

class NmeaParser {
public:
    // Passa un byte. Se completa un'epoca, la copia in out e restituisce true.
    bool feed(char c, uint32_t now_ms, GpsEpoch& out);

private:
    static constexpr size_t NMEA_LINE_MAX = 160;
    static constexpr size_t NMEA_FIELDS_MAX = 24;

    bool handle_line(char* line, uint32_t start_ms, GpsEpoch& out);

    char line_[NMEA_LINE_MAX];
    size_t len_ = 0;
    bool in_line_ = false;
    uint32_t line_start_ms_ = 0;

    bool active_ = false;
    GpsEpoch cur_ = {};
    char cur_key_[16] = {};
    int gsa_mode_ = 0;
    bool gga_seen_ = false;
    int gga_quality_ = 0;
    bool rmc_pos_ok_ = false;
};
