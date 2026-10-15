#include "beacon.h"

namespace beacon {

uint8_t age_ds(uint32_t elapsed_ms) {
    uint32_t ds = elapsed_ms / 100;
    return ds > 255 ? 255 : static_cast<uint8_t>(ds);
}

size_t pack(const Beacon& b, uint8_t* out, size_t cap) {
    if (b.n > MAX_NEIGHBORS) return 0;
    size_t size = HEADER_SIZE + static_cast<size_t>(b.n) * ENTRY_SIZE;
    if (cap < size) return 0;

    out[0] = MAGIC;
    out[1] = VERSION;
    out[2] = b.id;
    out[3] = static_cast<uint8_t>(b.seq & 0xFF);
    out[4] = static_cast<uint8_t>((b.seq >> 8) & 0xFF);
    out[5] = static_cast<uint8_t>((b.seq >> 16) & 0xFF);
    out[6] = static_cast<uint8_t>((b.seq >> 24) & 0xFF);
    out[7] = b.n;
    for (uint8_t i = 0; i < b.n; i++) {
        uint8_t* p = out + HEADER_SIZE + i * ENTRY_SIZE;
        p[0] = b.nb[i].id;
        p[1] = static_cast<uint8_t>(b.nb[i].rssi);
        p[2] = b.nb[i].age_ds;
        p[3] = b.nb[i].variance;
    }
    return size;
}

bool unpack(const uint8_t* data, size_t len, Beacon& b) {
    if (len < HEADER_SIZE) return false;
    if (data[0] != MAGIC || data[1] != VERSION) return false;
    uint8_t n = data[7];
    if (n > MAX_NEIGHBORS) return false;
    if (len != HEADER_SIZE + static_cast<size_t>(n) * ENTRY_SIZE) return false;

    b.id = data[2];
    b.seq = static_cast<uint32_t>(data[3]) | (static_cast<uint32_t>(data[4]) << 8) |
            (static_cast<uint32_t>(data[5]) << 16) | (static_cast<uint32_t>(data[6]) << 24);
    b.n = n;
    for (uint8_t i = 0; i < n; i++) {
        const uint8_t* p = data + HEADER_SIZE + i * ENTRY_SIZE;
        b.nb[i].id = p[0];
        b.nb[i].rssi = static_cast<int8_t>(p[1]);
        b.nb[i].age_ds = p[2];
        b.nb[i].variance = p[3];
    }
    return true;
}

}  // namespace beacon
