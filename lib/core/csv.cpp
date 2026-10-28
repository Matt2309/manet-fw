#include "csv.h"

#include <stdio.h>
#include <string.h>

#include "config.h"

namespace {

// Esito di snprintf -> caratteri scritti o 0 se non entra.
size_t esito(int w, size_t cap) { return (w > 0 && static_cast<size_t>(w) < cap) ? static_cast<size_t>(w) : 0; }

}  // namespace

size_t csv_header(char* buf, size_t cap, const char* fw, int id, const char* role,
                  const char* mac, int channel, int power_qdbm, int period_ms, int jitter_ms,
                  const char* rate, int proto) {
    int w = snprintf(buf, cap,
                     "# manet-fw %s id=%d ruolo=%s mac=%s canale=%d potenza_dbm=%.2f periodo_ms=%d "
                     "jitter_ms=%d rate=%s proto=%d\n",
                     fw, id, role, mac, channel, power_qdbm / 4.0, period_ms, jitter_ms, rate, proto);
    return esito(w, cap);
}

size_t csv_params(char* buf, size_t cap) {
    auto u = [](uint32_t v) { return static_cast<unsigned long>(v); };
    int w = snprintf(buf, cap,
                     "# param proto=%d max_nodi=%d tick_ms=%lu coda_rx=%d sentito_ms=%lu conflitto_capo_ms=%lu "
                     "finestra_ruolo_ms=%lu\n"
                     "# param flush_ms=%lu bat_log_ms=%lu spazio_min_pct=%d coda_log=%d buffer_log=%d "
                     "riga_log=%d rx_ridotta=%d\n"
                     "# param tasto_debounce_ms=%lu tasto_breve_max_ms=%lu tasto_lungo_ms=%lu display_ms=%lu "
                     "bat_bassa_pct=%d bat_cal=%.3f\n"
                     "# param kalman_sigma_a=%.3f kalman_r=%.2f kalman_pendenza0=%.1f kalman_riavvio_ms=%lu "
                     "fusione_eta_ms=%lu pre_dbm=%.1f pre_min_ms=%lu rilascio_margine_db=%.1f rilascio_ms=%lu "
                     "rilascio_ricezione=%.2f perso_ms=%lu kal_log_ms=%lu\n"
                     "# param gnss_sat_min=%d gnss_hdop_max=%.1f gnss_eta_ms=%lu gnss_finestra_ms=%lu gnss_campione_ms=%lu "
                     "gnss_cresce_m=%.1f gnss_ferma_m=%.1f gnss_r_cala_db=%.1f gnss_soglia_cresce_dbm=%.1f "
                     "gnss_soglia_ferma_dbm=%.1f gnss_ferma_conferma_ms=%lu allarmi_gnss=%d\n",
                     PROTO_VERSION, MAX_NODES, u(LOGIC_TICK_MS), RX_QUEUE_LEN, u(HEARD_WINDOW_MS),
                     u(LEADER_CONFLICT_MS), u(ROLE_WINDOW_MS), u(FLUSH_INTERVAL_MS), u(BAT_LOG_INTERVAL_MS),
                     FREE_SPACE_MIN_PCT, EVENT_QUEUE_LEN, LOG_BUFFER_BYTES, LOG_LINE_BYTES,
                     LOG_RX_REDUCED ? 1 : 0, u(BTN_DEBOUNCE_MS), u(BTN_SHORT_MAX_MS), u(BTN_LONG_MS),
                     u(DISPLAY_PERIOD_MS), BAT_LOW_PCT, static_cast<double>(BAT_CAL), KALMAN_SIGMA_A, KALMAN_R,
                     KALMAN_INITIAL_SLOPE_STD, u(KALMAN_REINIT_GAP_MS), u(FUSION_MAX_AGE_MS), PRE_ALARM_DBM,
                     u(PRE_ALARM_MIN_MS), RELEASE_MARGIN_DB, u(RELEASE_TIME_MS), RELEASE_RECEPTION,
                     u(LOST_SILENCE_MS), u(KAL_LOG_PERIOD_MS), GNSS_MIN_SAT, GNSS_MAX_HDOP, u(GNSS_MAX_AGE_MS),
                     u(GNSS_WINDOW_MS), u(GNSS_SAMPLE_MS), GNSS_GROW_M, GNSS_STILL_M, GNSS_R_DROP_DB,
                     GNSS_THRESHOLD_GROW_DBM, GNSS_THRESHOLD_STILL_DBM, u(GNSS_STILL_MIN_MS),
                     ALARM_USES_GNSS ? 1 : 0);
    return esito(w, cap);
}

size_t csv_tx(char* buf, size_t cap, uint32_t t_ms, uint32_t seq) {
    int w = snprintf(buf, cap, "TX,%lu,%lu\n", static_cast<unsigned long>(t_ms),
                     static_cast<unsigned long>(seq));
    return esito(w, cap);
}

size_t csv_rx(char* buf, size_t cap, uint32_t t_ms, const packets::Beacon& b, int rssi, bool reduced) {
    int w = snprintf(buf, cap, "RX,%lu,%d,%u,%d,", static_cast<unsigned long>(t_ms), b.sender, b.seq, rssi);
    if (w <= 0 || static_cast<size_t>(w) >= cap) return 0;
    size_t pos = static_cast<size_t>(w);
    if (!reduced) {
        for (int i = 0; i < b.n; i++) {
            const packets::BeaconEntry& e = b.nb[i];
            int k = snprintf(buf + pos, cap - pos, "%s%d:%d:%d:%d:%d", i > 0 ? ";" : "", e.id, e.signal,
                             e.age_ds, e.sigma, e.slope);
            if (k <= 0 || static_cast<size_t>(k) >= cap - pos) return 0;
            pos += static_cast<size_t>(k);
        }
        char t_s[8] = "";
        if (b.t_s != packets::T_NOT_STARTED) snprintf(t_s, sizeof(t_s), "%u", b.t_s);
        int k = snprintf(buf + pos, cap - pos, ",%d,%s,%d", b.hops, t_s, packets::beacon_status(b));
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

size_t csv_kal(char* buf, size_t cap, uint32_t t_ms, int id, float signal, float slope, float sigma,
               int age_ds) {
    int w = snprintf(buf, cap, "KAL,%lu,%d,%.2f,%.3f,%.2f,%d\n", static_cast<unsigned long>(t_ms), id,
                     static_cast<double>(signal), static_cast<double>(slope), static_cast<double>(sigma), age_ds);
    return esito(w, cap);
}

size_t csv_det(char* buf, size_t cap, uint32_t t_ms, DetVersion v, DetKind kind, bool start, int id,
               float r, float threshold) {
    static const char* const KIND[] = {"pre", "perso", "sorda"};
    char r_txt[16] = "", thr_txt[16] = "";
    if (kind == DetKind::PreAlarm) {
        snprintf(r_txt, sizeof(r_txt), "%.2f", static_cast<double>(r));
        snprintf(thr_txt, sizeof(thr_txt), "%.1f", static_cast<double>(threshold));
    }
    int w = snprintf(buf, cap, "DET,%lu,%s,%s,%s,%d,%s,%s\n", static_cast<unsigned long>(t_ms),
                     v == DetVersion::Gnss ? "gnss" : "base", KIND[static_cast<int>(kind)],
                     start ? "inizio" : "fine", id, r_txt, thr_txt);
    return esito(w, cap);
}

size_t csv_tel(char* buf, size_t cap, uint32_t t_ms, TelAction action, int id, int t_s, int peer,
               int queued, int tries) {
    static const char* const ACTION[] = {"prodotto", "inviato", "confermato", "ricevuto", "coda"};
    int w = snprintf(buf, cap, "TEL,%lu,%s,%d,%d,%d,%d,%d\n", static_cast<unsigned long>(t_ms),
                     ACTION[static_cast<int>(action)], id, t_s, peer, queued, tries);
    return esito(w, cap);
}

size_t csv_cmd(char* buf, size_t cap, uint32_t t_ms, packets::Cmd cmd, CmdOrigin origin, int seq,
               int elapsed_ds) {
    int w = snprintf(buf, cap, "CMD,%lu,%s,%s,%d,%d\n", static_cast<unsigned long>(t_ms),
                     cmd == packets::Cmd::Start ? "start" : "stop", origin == CmdOrigin::App ? "app" : "radio",
                     seq, elapsed_ds);
    return esito(w, cap);
}

size_t csv_ble(char* buf, size_t cap, uint32_t t_ms, BleEvent event, int detail) {
    int w = snprintf(buf, cap, "BLE,%lu,%s,%d\n", static_cast<unsigned long>(t_ms),
                     event == BleEvent::Connected ? "connesso" : "caduto", detail);
    return esito(w, cap);
}
