#include "csv.h"

#include <stdio.h>
#include <string.h>

namespace {

// Esito di snprintf -> caratteri scritti o 0 se non entra.
size_t esito(int w, size_t cap) { return (w > 0 && static_cast<size_t>(w) < cap) ? static_cast<size_t>(w) : 0; }

}  // namespace

size_t csv_header(char* buf, size_t cap, const char* fw, int id, const char* mac,
                  int channel, int power_qdbm, int period_ms, int jitter_ms,
                  const char* rate) {
    int w = snprintf(buf, cap,
                     "# manet-fw %s id=%d mac=%s canale=%d potenza_dbm=%.2f periodo_ms=%d "
                     "jitter_ms=%d rate=%s\n",
                     fw, id, mac, channel, power_qdbm / 4.0, period_ms, jitter_ms, rate);
    return esito(w, cap);
}

size_t csv_tx(char* buf, size_t cap, uint32_t t_ms, uint32_t seq) {
    int w = snprintf(buf, cap, "TX,%lu,%lu\n", static_cast<unsigned long>(t_ms),
                     static_cast<unsigned long>(seq));
    return esito(w, cap);
}

size_t csv_rx(char* buf, size_t cap, uint32_t t_ms, int src, uint32_t seq, int rssi,
              const beacon::Entry* tab, int n) {
    int w = snprintf(buf, cap, "RX,%lu,%d,%lu,%d,", static_cast<unsigned long>(t_ms), src,
                     static_cast<unsigned long>(seq), rssi);
    if (w <= 0 || static_cast<size_t>(w) >= cap) return 0;
    size_t pos = static_cast<size_t>(w);
    for (int i = 0; i < n; i++) {
        int k = snprintf(buf + pos, cap - pos, "%s%d:%d:%d:%d", i > 0 ? ";" : "", tab[i].id,
                         tab[i].rssi, tab[i].age_ds, tab[i].variance);
        if (k <= 0 || static_cast<size_t>(k) >= cap - pos) return 0;
        pos += static_cast<size_t>(k);
    }
    if (pos + 2 > cap) return 0;
    buf[pos++] = '\n';
    buf[pos] = '\0';
    return pos;
}

size_t csv_gps(char* buf, size_t cap, uint32_t t_ms, const GpsEpoch& e) {
    char utc[32] = "";
    if (e.has_time && e.has_date) {
        snprintf(utc, sizeof(utc), "%04d-%02d-%02dT%02d:%02d:%02d.%02dZ", e.year, e.month, e.day,
                 e.hour, e.minute, e.second, e.centisecond);
    }
    char lat[24] = "", lon[24] = "", alt[16] = "", vel[16] = "", rot[16] = "", hdop[16] = "",
         sat[8] = "";
    if (e.has_pos) {
        snprintf(lat, sizeof(lat), "%.7f", e.lat);
        snprintf(lon, sizeof(lon), "%.7f", e.lon);
    }
    if (e.has_alt) snprintf(alt, sizeof(alt), "%.1f", static_cast<double>(e.alt));
    if (e.has_speed) snprintf(vel, sizeof(vel), "%.2f", static_cast<double>(e.speed_kmh));
    if (e.has_course) snprintf(rot, sizeof(rot), "%.1f", static_cast<double>(e.course));
    if (e.has_hdop) snprintf(hdop, sizeof(hdop), "%.2f", static_cast<double>(e.hdop));
    if (e.has_sat) snprintf(sat, sizeof(sat), "%d", e.sat);

    int w = snprintf(buf, cap, "GPS,%lu,%s,%s,%s,%s,%s,%s,%s,%s,%d\n",
                     static_cast<unsigned long>(t_ms), utc, lat, lon, alt, vel, rot, hdop, sat,
                     e.fix);
    return esito(w, cap);
}

size_t csv_evt(char* buf, size_t cap, uint32_t t_ms, int n) {
    int w = snprintf(buf, cap, "EVT,%lu,%d\n", static_cast<unsigned long>(t_ms), n);
    return esito(w, cap);
}

size_t csv_bat(char* buf, size_t cap, uint32_t t_ms, int mv) {
    int w = snprintf(buf, cap, "BAT,%lu,%d\n", static_cast<unsigned long>(t_ms), mv);
    return esito(w, cap);
}

size_t csv_sys(char* buf, size_t cap, uint32_t t_ms, const char* msg) {
    // Il messaggio non deve rompere la riga: a capo e virgole diventano spazi.
    char pulito[128];
    size_t i = 0;
    for (; msg[i] != '\0' && i < sizeof(pulito) - 1; i++) {
        char c = msg[i];
        pulito[i] = (c == '\n' || c == '\r' || c == ',') ? ' ' : c;
    }
    pulito[i] = '\0';
    int w = snprintf(buf, cap, "SYS,%lu,%s\n", static_cast<unsigned long>(t_ms), pulito);
    return esito(w, cap);
}
