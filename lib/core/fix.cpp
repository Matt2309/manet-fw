#include "fix.h"

#include "config.h"

bool fix_valid(const GpsEpoch& e) {
    return e.fix == 3 && e.has_pos && e.has_sat && e.sat >= GNSS_MIN_SAT && e.has_hdop &&
           e.hdop <= GNSS_MAX_HDOP;
}

GnssSample sample_from_epoch(const GpsEpoch& e) {
    GnssSample s = {};
    s.t_ms = e.t_ms;
    s.valid = fix_valid(e);
    if (s.valid) {
        s.pos.lat_e7 = packets::degrees_to_e7(e.lat);
        s.pos.lon_e7 = packets::degrees_to_e7(e.lon);
        s.pos.hdop_dm = packets::quantize_u8(e.hdop, 0.1);
    }
    return s;
}
