#include "seen.h"

namespace {

bool bit(const uint8_t* bits, unsigned i) { return (bits[i / 8] >> (i % 8)) & 1u; }
void set_bit(uint8_t* bits, unsigned i, bool v) {
    if (v) {
        bits[i / 8] = static_cast<uint8_t>(bits[i / 8] | (1u << (i % 8)));
    } else {
        bits[i / 8] = static_cast<uint8_t>(bits[i / 8] & ~(1u << (i % 8)));
    }
}

}  // namespace

bool SeenRecords::add(uint8_t origin, uint16_t t_s) {
    if (origin > MAX_NODES) return false;
    Slot& s = slot_[origin];
    if (!s.valid) {
        memset(s.bits, 0, sizeof(s.bits));
        s.valid = true;
        s.newest = t_s;
        set_bit(s.bits, t_s % SEEN_RECORD_WINDOW, true);
        return true;
    }
    if (t_s > s.newest) {
        unsigned gap = t_s - s.newest;
        if (gap >= SEEN_RECORD_WINDOW) {
            memset(s.bits, 0, sizeof(s.bits));
        } else {
            for (unsigned k = 1; k <= gap; k++) set_bit(s.bits, (s.newest + k) % SEEN_RECORD_WINDOW, false);
        }
        s.newest = t_s;
        set_bit(s.bits, t_s % SEEN_RECORD_WINDOW, true);
        return true;
    }
    if (s.newest - t_s >= SEEN_RECORD_WINDOW) return false;  // troppo vecchio per essere ricordato
    unsigned i = t_s % SEEN_RECORD_WINDOW;
    if (bit(s.bits, i)) return false;
    set_bit(s.bits, i, true);
    return true;
}

bool SeenSeq::add(uint8_t creator, uint8_t seq) {
    if (creator > MAX_NODES) return false;
    Slot& s = slot_[creator];
    if (!s.valid) {
        memset(s.bits, 0, sizeof(s.bits));
        s.valid = true;
        s.newest = seq;
        set_bit(s.bits, seq, true);
        return true;
    }
    uint8_t d = static_cast<uint8_t>(seq - s.newest);
    if (d == 0) return false;
    if (d < 128) {  // piu' recente: i numeri in mezzo non sono stati visti
        for (unsigned k = 1; k <= d; k++) set_bit(s.bits, static_cast<uint8_t>(s.newest + k), false);
        s.newest = seq;
        set_bit(s.bits, seq, true);
        return true;
    }
    if (bit(s.bits, seq)) return false;
    set_bit(s.bits, seq, true);
    return true;
}
