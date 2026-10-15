#include "nmea.h"

#include <stdlib.h>
#include <string.h>

namespace {

int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }

// Legge n cifre da s; -1 se non sono tutte cifre.
int digits(const char* s, int n) {
    int v = 0;
    for (int i = 0; i < n; i++) {
        if (!is_digit(s[i])) return -1;
        v = v * 10 + (s[i] - '0');
    }
    return v;
}

// "hhmmss" o "hhmmss.ss..." -> campi; false se malformato.
bool parse_time(const char* f, GpsEpoch& e) {
    if (strlen(f) < 6) return false;
    int h = digits(f, 2), m = digits(f + 2, 2), s = digits(f + 4, 2);
    if (h < 0 || m < 0 || s < 0) return false;
    int cs = 0;
    if (f[6] == '.') {
        // frazione di secondo con 1, 2 o piu' cifre -> centesimi
        int d1 = is_digit(f[7]) ? f[7] - '0' : 0;
        int d2 = (is_digit(f[7]) && is_digit(f[8])) ? f[8] - '0' : 0;
        cs = d1 * 10 + d2;
    }
    e.hour = h;
    e.minute = m;
    e.second = s;
    e.centisecond = cs;
    e.has_time = true;
    return true;
}

// "ddmmyy" -> campi (anno 2000+).
bool parse_date(const char* f, GpsEpoch& e) {
    if (strlen(f) < 6) return false;
    int d = digits(f, 2), m = digits(f + 2, 2), y = digits(f + 4, 2);
    if (d < 1 || m < 1 || m > 12 || y < 0) return false;
    e.day = d;
    e.month = m;
    e.year = 2000 + y;
    e.has_date = true;
    return true;
}

// "ddmm.mmmm" / "dddmm.mmmm" + emisfero -> gradi decimali.
bool parse_coord(const char* v, const char* hemi, int deg_digits, double& out) {
    if (v[0] == '\0' || hemi[0] == '\0') return false;
    if (digits(v, deg_digits) < 0) return false;
    double deg = digits(v, deg_digits);
    double min = atof(v + deg_digits);
    double r = deg + min / 60.0;
    if (hemi[0] == 'S' || hemi[0] == 'W') r = -r;
    out = r;
    return true;
}

}  // namespace

bool nmea_checksum_ok(const char* sentence) {
    if (sentence[0] != '$') return false;
    uint8_t cs = 0;
    const char* p = sentence + 1;
    while (*p != '\0' && *p != '*') cs ^= static_cast<uint8_t>(*p++);
    if (*p != '*') return false;
    int hi = hex_val(p[1]), lo = (hi >= 0) ? hex_val(p[2]) : -1;
    if (hi < 0 || lo < 0) return false;
    return cs == static_cast<uint8_t>(hi * 16 + lo);
}

bool NmeaParser::feed(char c, uint32_t now_ms, GpsEpoch& out) {
    if (c == '$') {
        in_line_ = true;
        len_ = 0;
        line_start_ms_ = now_ms;
    }
    if (!in_line_) return false;

    if (c == '\r' || c == '\n') {
        in_line_ = false;
        if (len_ == 0) return false;
        line_[len_] = '\0';
        return handle_line(line_, line_start_ms_, out);
    }
    if (len_ >= NMEA_LINE_MAX - 1) {  // riga troppo lunga: scartata
        in_line_ = false;
        return false;
    }
    line_[len_++] = c;
    return false;
}

bool NmeaParser::handle_line(char* line, uint32_t start_ms, GpsEpoch& out) {
    if (!nmea_checksum_ok(line)) return false;

    // Toglie "$" e il checksum, poi separa i campi sul posto.
    char* star = strchr(line, '*');
    *star = '\0';
    char* f[NMEA_FIELDS_MAX];
    size_t nf = 0;
    f[nf++] = line + 1;
    for (char* p = line + 1; *p != '\0' && nf < NMEA_FIELDS_MAX; p++) {
        if (*p == ',') {
            *p = '\0';
            f[nf++] = p + 1;
        }
    }
    // I campi mancanti in fondo valgono stringa vuota.
    static char vuoto[] = "";
    for (size_t i = nf; i < NMEA_FIELDS_MAX; i++) f[i] = vuoto;

    // Indirizzo: 2 lettere di talker + 3 di tipo. Le frasi proprietarie ($P...) si ignorano.
    if (strlen(f[0]) != 5 || f[0][0] == 'P') return false;
    const char* type = f[0] + 2;

    bool is_rmc = strcmp(type, "RMC") == 0;
    bool is_gga = strcmp(type, "GGA") == 0;
    bool is_gsa = strcmp(type, "GSA") == 0;
    if (!is_rmc && !is_gga && !is_gsa) return false;

    bool completed = false;

    if (is_rmc || is_gga) {
        const char* time_f = f[1];
        char key[sizeof(cur_key_)];
        strncpy(key, time_f, sizeof(key) - 1);
        key[sizeof(key) - 1] = '\0';

        bool new_epoch;
        if (!active_) {
            new_epoch = true;
        } else if (key[0] != '\0') {
            new_epoch = strcmp(key, cur_key_) != 0;
        } else {
            // senza ora: si taglia a 900 ms dall'inizio dell'epoca
            new_epoch = cur_key_[0] == '\0' && (start_ms - cur_.t_ms) >= 900;
        }

        if (new_epoch) {
            if (active_) {
                // Chiude l'epoca precedente.
                GpsEpoch e = cur_;
                if (gga_seen_ && gga_quality_ == 0) {
                    e.fix = 0;
                } else if (gsa_mode_ >= 2) {
                    e.fix = gsa_mode_;
                } else if (gga_seen_ && gga_quality_ > 0) {
                    e.fix = 2;
                } else {
                    e.fix = 0;
                }
                if (e.fix == 0) {  // senza fix la posizione non e' attendibile
                    e.has_pos = false;
                    e.has_alt = false;
                }
                out = e;
                completed = true;
            }
            cur_ = GpsEpoch{};
            cur_.t_ms = start_ms;
            strncpy(cur_key_, key, sizeof(cur_key_) - 1);
            cur_key_[sizeof(cur_key_) - 1] = '\0';
            active_ = true;
            gsa_mode_ = 0;
            gga_seen_ = false;
            gga_quality_ = 0;
            rmc_pos_ok_ = false;
        }

        if (!cur_.has_time) parse_time(time_f, cur_);

        if (is_rmc) {
            // $xxRMC,time,status,lat,N,lon,E,speed_kn,course,date,...
            bool valid = f[2][0] == 'A';
            if (valid) {
                if (!cur_.has_pos) {
                    double la, lo;
                    if (parse_coord(f[3], f[4], 2, la) && parse_coord(f[5], f[6], 3, lo)) {
                        cur_.lat = la;
                        cur_.lon = lo;
                        rmc_pos_ok_ = true;
                    }
                }
                if (f[7][0] != '\0') {
                    cur_.speed_kmh = static_cast<float>(atof(f[7]) * 1.852);
                    cur_.has_speed = true;
                }
                if (f[8][0] != '\0') {
                    cur_.course = static_cast<float>(atof(f[8]));
                    cur_.has_course = true;
                }
            }
            parse_date(f[9], cur_);
        } else {
            // $xxGGA,time,lat,N,lon,E,quality,sats,hdop,alt,M,...
            gga_seen_ = true;
            gga_quality_ = f[6][0] != '\0' ? atoi(f[6]) : 0;
            if (f[7][0] != '\0') {
                cur_.sat = atoi(f[7]);
                cur_.has_sat = true;
            }
            if (f[8][0] != '\0') {
                cur_.hdop = static_cast<float>(atof(f[8]));
                cur_.has_hdop = true;
            }
            if (gga_quality_ > 0) {
                double la, lo;
                if (parse_coord(f[2], f[3], 2, la) && parse_coord(f[4], f[5], 3, lo)) {
                    cur_.lat = la;
                    cur_.lon = lo;
                    rmc_pos_ok_ = true;
                }
                if (f[9][0] != '\0') {
                    cur_.alt = static_cast<float>(atof(f[9]));
                    cur_.has_alt = true;
                }
            }
        }
        cur_.has_pos = rmc_pos_ok_;
    } else if (is_gsa && active_) {
        // $xxGSA,mode_sel,fix_mode,...
        int m = f[2][0] != '\0' ? atoi(f[2]) : 0;
        if (m > gsa_mode_) gsa_mode_ = m;
    }

    return completed;
}
