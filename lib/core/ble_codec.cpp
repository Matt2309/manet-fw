#include "ble_codec.h"

namespace ble {
namespace {

void put32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFF);
}

uint32_t get32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

size_t encode_state(uint32_t seq, uint32_t t, const Row* rows, size_t n, uint8_t* out, size_t cap) {
    if (n > MAX_RUNNERS) return 0;
    if ((seq == 0) != (t == T_NOT_STARTED)) return 0;
    size_t size = STATE_HEADER_SIZE + n * ROW_SIZE;
    if (cap < size) return 0;
    out[0] = VERSION;
    out[1] = static_cast<uint8_t>(MessageType::State);
    put32(out + 2, seq);
    put32(out + 6, t);
    out[10] = static_cast<uint8_t>(n);
    for (size_t i = 0; i < n; i++) {
        uint8_t* p = out + STATE_HEADER_SIZE + i * ROW_SIZE;
        p[0] = rows[i].id;
        p[1] = static_cast<uint8_t>(rows[i].state);
        p[2] = static_cast<uint8_t>(rows[i].r);
        p[3] = rows[i].battery > 100 ? 100 : rows[i].battery;
        p[4] = static_cast<uint8_t>(rows[i].age_s & 0xFF);
        p[5] = static_cast<uint8_t>(rows[i].age_s >> 8);
    }
    return size;
}

size_t encode_event(uint32_t seq, uint32_t t, uint8_t runner, EventType type, uint8_t* out, size_t cap) {
    if (cap < EVENT_SIZE || seq == 0 || t == T_NOT_STARTED) return 0;
    out[0] = VERSION;
    out[1] = static_cast<uint8_t>(MessageType::Event);
    put32(out + 2, seq);
    put32(out + 6, t);
    out[10] = runner;
    out[11] = static_cast<uint8_t>(type);
    return EVENT_SIZE;
}

size_t encode_confirm(CommandCode cmd, bool ok, uint8_t* out, size_t cap) {
    if (cap < CONFIRM_SIZE) return 0;
    out[0] = VERSION;
    out[1] = static_cast<uint8_t>(MessageType::Confirm);
    out[2] = static_cast<uint8_t>(cmd);
    out[3] = ok ? 0 : 1;
    return CONFIRM_SIZE;
}

size_t encode_confirm_resume(bool ok, uint32_t last_seq, uint8_t* out, size_t cap) {
    if (cap < CONFIRM_RESUME_SIZE) return 0;
    encode_confirm(CommandCode::ResumeFrom, ok, out, cap);
    put32(out + 4, last_seq);
    return CONFIRM_RESUME_SIZE;
}

bool decode_command(const uint8_t* data, size_t len, Command& c) {
    if (len < 2 || data[0] != VERSION) return false;
    c = Command{};
    switch (data[1]) {
        case static_cast<uint8_t>(CommandCode::Start):
        case static_cast<uint8_t>(CommandCode::Stop):
            if (len != 2) return false;
            c.code = static_cast<CommandCode>(data[1]);
            return true;
        case static_cast<uint8_t>(CommandCode::CloseAlarm):
            if (len != 4 || data[3] > static_cast<uint8_t>(AlarmKind::Sos)) return false;
            c.code = CommandCode::CloseAlarm;
            c.id = data[2];
            c.kind = static_cast<AlarmKind>(data[3]);
            return true;
        case static_cast<uint8_t>(CommandCode::ResumeFrom):
            if (len != 6) return false;
            c.code = CommandCode::ResumeFrom;
            c.last_seq = get32(data + 2);
            return true;
    }
    return false;
}

bool message_seq(const uint8_t* msg, size_t len, uint32_t& seq) {
    if (len < EVENT_SIZE || msg[0] != VERSION) return false;
    if (msg[1] != static_cast<uint8_t>(MessageType::State) && msg[1] != static_cast<uint8_t>(MessageType::Event)) return false;
    seq = get32(msg + 2);
    return true;
}

}  // namespace ble
