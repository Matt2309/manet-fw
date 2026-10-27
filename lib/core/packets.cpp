#include "packets.h"

#include <math.h>
#include <string.h>

namespace packets {
namespace {

constexpr uint8_t STATUS_STOPPED = 1 << 4;
constexpr uint8_t STATUS_SOS = 1 << 5;
constexpr uint8_t STATUS_POS = 1 << 6;
constexpr uint8_t TEL_SOS = 1 << 4;
constexpr uint8_t TEL_ISOLATED = 1 << 5;
constexpr uint8_t TEL_POS = 1 << 6;
constexpr uint8_t BATTERY_MASK = 0x0F;

constexpr size_t BEACON_FIXED = HEADER_SIZE + 7;       // seq 2, salti 1, t_s 2, stato 1, n 1
constexpr size_t TEL_RECORD_FIXED = 5;                 // id 1, t_s 2, flag 1, n 1
constexpr size_t TEL_FIXED = HEADER_SIZE + 1;          // n record
constexpr size_t ALARM_SIZE = HEADER_SIZE + 6;         // seq, id, tipo, t_s 2, segnale
constexpr size_t COMMAND_SIZE = HEADER_SIZE + 4;       // seq, comando, decimi 2

void put16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>(v >> 8);
}

uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

void put32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFF);
}

uint32_t get32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

void put_header(uint8_t* out, Type t, uint8_t sender) {
    out[0] = MAGIC;
    out[1] = PROTO_VERSION;
    out[2] = static_cast<uint8_t>(t);
    out[3] = sender;
}

void put_pos(uint8_t* p, const Position& pos) {
    put32(p, static_cast<uint32_t>(pos.lat_e7));
    put32(p + 4, static_cast<uint32_t>(pos.lon_e7));
    p[8] = pos.hdop_dm;
}

void get_pos(const uint8_t* p, Position& pos) {
    pos.lat_e7 = static_cast<int32_t>(get32(p));
    pos.lon_e7 = static_cast<int32_t>(get32(p + 4));
    pos.hdop_dm = p[8];
}

// Controlla intestazione e tipo atteso.
bool header_is(const uint8_t* data, size_t len, Type want, uint8_t& sender) {
    Type t;
    return read_header(data, len, t, sender) && t == want;
}

}  // namespace

bool read_header(const uint8_t* data, size_t len, Type& type, uint8_t& sender) {
    if (len < HEADER_SIZE) return false;
    if (data[0] != MAGIC || data[1] != PROTO_VERSION) return false;
    if (data[2] < static_cast<uint8_t>(Type::Beacon) || data[2] > static_cast<uint8_t>(Type::Command)) {
        return false;
    }
    type = static_cast<Type>(data[2]);
    sender = data[3];
    return true;
}

// --- Beacon ------------------------------------------------------------------

size_t beacon_size(uint8_t n, bool has_pos) {
    return BEACON_FIXED + (has_pos ? POS_SIZE : 0) + static_cast<size_t>(n) * ENTRY_SIZE;
}

size_t pack_beacon(const Beacon& b, uint8_t* out, size_t cap) {
    if (b.n > MAX_NEIGHBORS) return 0;
    size_t size = beacon_size(b.n, b.has_pos);
    if (cap < size) return 0;

    put_header(out, Type::Beacon, b.sender);
    put16(out + 4, b.seq);
    out[6] = b.hops;
    put16(out + 7, b.t_s);
    uint8_t status = b.battery > BATTERY_MAX ? BATTERY_MAX : b.battery;
    if (b.stopped) status |= STATUS_STOPPED;
    if (b.sos) status |= STATUS_SOS;
    if (b.has_pos) status |= STATUS_POS;
    out[9] = status;
    out[10] = b.n;
    uint8_t* p = out + BEACON_FIXED;
    if (b.has_pos) {
        put_pos(p, b.pos);
        p += POS_SIZE;
    }
    for (uint8_t i = 0; i < b.n; i++, p += ENTRY_SIZE) {
        p[0] = b.nb[i].id;
        p[1] = static_cast<uint8_t>(b.nb[i].signal);
        p[2] = static_cast<uint8_t>(b.nb[i].slope);
        p[3] = b.nb[i].sigma;
        p[4] = b.nb[i].age_ds;
    }
    return size;
}

bool unpack_beacon(const uint8_t* data, size_t len, Beacon& b) {
    uint8_t sender;
    if (!header_is(data, len, Type::Beacon, sender)) return false;
    if (len < BEACON_FIXED) return false;
    uint8_t status = data[9];
    bool has_pos = (status & STATUS_POS) != 0;
    uint8_t n = data[10];
    if (n > MAX_NEIGHBORS || len != beacon_size(n, has_pos)) return false;

    b.sender = sender;
    b.seq = get16(data + 4);
    b.hops = data[6];
    b.t_s = get16(data + 7);
    b.battery = status & BATTERY_MASK;
    b.stopped = (status & STATUS_STOPPED) != 0;
    b.sos = (status & STATUS_SOS) != 0;
    b.has_pos = has_pos;
    b.n = n;
    const uint8_t* p = data + BEACON_FIXED;
    if (has_pos) {
        get_pos(p, b.pos);
        p += POS_SIZE;
    } else {
        b.pos = Position{};
    }
    for (uint8_t i = 0; i < n; i++, p += ENTRY_SIZE) {
        b.nb[i] = BeaconEntry{p[0], static_cast<int8_t>(p[1]), static_cast<int8_t>(p[2]), p[3], p[4]};
    }
    return true;
}

// --- Telemetria --------------------------------------------------------------

size_t telemetry_record_size(uint8_t n, bool has_pos) {
    return TEL_RECORD_FIXED + (has_pos ? POS_SIZE : 0) + static_cast<size_t>(n) * ENTRY_SIZE;
}

namespace {

void write_record(const TelRecord& r, uint8_t* p) {
    p[0] = r.id;
    put16(p + 1, r.t_s);
    uint8_t flag = r.battery > BATTERY_MAX ? BATTERY_MAX : r.battery;
    if (r.sos) flag |= TEL_SOS;
    if (r.isolated) flag |= TEL_ISOLATED;
    if (r.has_pos) flag |= TEL_POS;
    p[3] = flag;
    p[4] = r.n;
    p += TEL_RECORD_FIXED;
    if (r.has_pos) {
        put_pos(p, r.pos);
        p += POS_SIZE;
    }
    for (uint8_t i = 0; i < r.n; i++, p += ENTRY_SIZE) {
        p[0] = r.nb[i].id;
        p[1] = static_cast<uint8_t>(r.nb[i].signal);
        p[2] = r.nb[i].sigma;
        p[3] = r.nb[i].age_ds;
        p[4] = r.nb[i].alarm;
    }
}

}  // namespace

size_t pack_telemetry(uint8_t sender, const TelRecord* recs, size_t count, uint8_t* out, size_t cap,
                      size_t& packed) {
    packed = 0;
    if (cap > MAX_SIZE) cap = MAX_SIZE;
    size_t used = TEL_FIXED;
    while (packed < count && packed < 255) {
        const TelRecord& r = recs[packed];
        if (r.n > MAX_NEIGHBORS) break;
        size_t sz = telemetry_record_size(r.n, r.has_pos);
        if (used + sz > cap) break;
        write_record(r, out + used);
        used += sz;
        packed++;
    }
    if (packed == 0) return 0;
    put_header(out, Type::Telemetry, sender);
    out[HEADER_SIZE] = static_cast<uint8_t>(packed);
    return used;
}

bool unpack_telemetry(const uint8_t* data, size_t len, uint8_t& sender, TelRecord* recs, size_t max,
                      size_t& count) {
    count = 0;
    if (!header_is(data, len, Type::Telemetry, sender)) return false;
    if (len < TEL_FIXED) return false;
    size_t n_rec = data[HEADER_SIZE];
    if (n_rec == 0 || n_rec > max) return false;

    size_t pos = TEL_FIXED;
    for (size_t k = 0; k < n_rec; k++) {
        if (len - pos < TEL_RECORD_FIXED) return false;
        const uint8_t* p = data + pos;
        uint8_t flag = p[3];
        bool has_pos = (flag & TEL_POS) != 0;
        uint8_t n = p[4];
        if (n > MAX_NEIGHBORS) return false;
        size_t sz = telemetry_record_size(n, has_pos);
        if (len - pos < sz) return false;

        TelRecord& r = recs[k];
        r.id = p[0];
        r.t_s = get16(p + 1);
        r.battery = flag & BATTERY_MASK;
        r.sos = (flag & TEL_SOS) != 0;
        r.isolated = (flag & TEL_ISOLATED) != 0;
        r.has_pos = has_pos;
        r.n = n;
        p += TEL_RECORD_FIXED;
        if (has_pos) {
            get_pos(p, r.pos);
            p += POS_SIZE;
        } else {
            r.pos = Position{};
        }
        for (uint8_t i = 0; i < n; i++, p += ENTRY_SIZE) {
            r.nb[i] = TelEntry{p[0], static_cast<int8_t>(p[1]), p[2], p[3], p[4]};
        }
        pos += sz;
    }
    if (pos != len) return false;
    count = n_rec;
    return true;
}

// --- Allarme -----------------------------------------------------------------

size_t pack_alarm(const Alarm& a, uint8_t* out, size_t cap) {
    size_t size = ALARM_SIZE + (a.has_pos ? POS_SIZE : 0);
    if (cap < size) return 0;
    put_header(out, Type::Alarm, a.sender);
    out[4] = a.seq;
    out[5] = a.runner;
    out[6] = static_cast<uint8_t>(a.type);
    put16(out + 7, a.last_t_s);
    out[9] = static_cast<uint8_t>(a.last_signal);
    if (a.has_pos) put_pos(out + ALARM_SIZE, a.pos);
    return size;
}

bool unpack_alarm(const uint8_t* data, size_t len, Alarm& a) {
    uint8_t sender;
    if (!header_is(data, len, Type::Alarm, sender)) return false;
    if (len != ALARM_SIZE && len != ALARM_SIZE + POS_SIZE) return false;
    if (data[6] < static_cast<uint8_t>(AlarmType::PreAlarm) || data[6] > static_cast<uint8_t>(AlarmType::End)) {
        return false;
    }
    a.sender = sender;
    a.seq = data[4];
    a.runner = data[5];
    a.type = static_cast<AlarmType>(data[6]);
    a.last_t_s = get16(data + 7);
    a.last_signal = static_cast<int8_t>(data[9]);
    a.has_pos = (len == ALARM_SIZE + POS_SIZE);
    if (a.has_pos) {
        get_pos(data + ALARM_SIZE, a.pos);
    } else {
        a.pos = Position{};
    }
    return true;
}

// --- Comando -----------------------------------------------------------------

size_t pack_command(const Command& c, uint8_t* out, size_t cap) {
    if (cap < COMMAND_SIZE) return 0;
    put_header(out, Type::Command, c.sender);
    out[4] = c.seq;
    out[5] = static_cast<uint8_t>(c.cmd);
    put16(out + 6, c.elapsed_ds);
    return COMMAND_SIZE;
}

bool unpack_command(const uint8_t* data, size_t len, Command& c) {
    uint8_t sender;
    if (!header_is(data, len, Type::Command, sender)) return false;
    if (len != COMMAND_SIZE) return false;
    if (data[5] < static_cast<uint8_t>(Cmd::Start) || data[5] > static_cast<uint8_t>(Cmd::Stop)) return false;
    c.sender = sender;
    c.seq = data[4];
    c.cmd = static_cast<Cmd>(data[5]);
    c.elapsed_ds = get16(data + 6);
    return true;
}

// --- Quantizzazione ----------------------------------------------------------

uint8_t age_ds(uint32_t elapsed_ms) {
    uint32_t ds = elapsed_ms / 100;
    return ds > 255 ? 255 : static_cast<uint8_t>(ds);
}

int8_t quantize_i8(float v, float step) {
    float q = roundf(v / step);
    if (q > 127.0f) return 127;
    if (q < -128.0f) return -128;
    return static_cast<int8_t>(q);
}

uint8_t quantize_u8(float v, float step) {
    float q = roundf(v / step);
    if (q < 0.0f) return 0;
    if (q > 254.0f) return 254;
    return static_cast<uint8_t>(q);
}

uint8_t battery_code(int percent) {
    if (percent <= 0) return 0;
    if (percent >= 100) return BATTERY_MAX;
    return static_cast<uint8_t>((percent * BATTERY_MAX + 50) / 100);
}

int battery_percent(uint8_t code) {
    if (code >= BATTERY_MAX) return 100;
    return (code * 100 + BATTERY_MAX / 2) / BATTERY_MAX;
}

int32_t degrees_to_e7(double deg) { return static_cast<int32_t>(lround(deg * 1e7)); }

}  // namespace packets
