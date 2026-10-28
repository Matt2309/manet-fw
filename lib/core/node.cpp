#include "node.h"

#include "config.h"

bool Node::on_packet(const uint8_t* data, size_t len, int8_t rssi, uint32_t now_ms, packets::Beacon& b) {
    packets::Type type;
    uint8_t sender;
    if (!packets::read_header(data, len, type, sender)) return false;
    if (sender == 0 || sender > MAX_NODES || sender == id_) return false;
    if (type != packets::Type::Beacon) return false;
    if (!packets::unpack_beacon(data, len, b)) return false;

    table_.update(sender, rssi, now_ms);
    detector_.on_beacon(b, rssi, now_ms);
    if (b.hops == 0) {
        leader_seen_ = true;
        leader_seen_ms_ = now_ms;
    }
    return true;
}

uint8_t Node::hops() const {
    // Il percorso verso il capogruppo arriva con la fase 4.
    return role_ == Role::Leader ? 0 : packets::HOPS_ISOLATED;
}

size_t Node::build_beacon(uint32_t now_ms, int battery_pct, uint8_t* out, size_t cap, packets::Beacon& sent) {
    sent = packets::Beacon{};
    sent.sender = id_;
    sent.seq = seq_++;
    sent.hops = hops();
    sent.t_s = packets::T_NOT_STARTED;
    sent.has_pos = detector_.own_position(now_ms, sent.pos);
    sent.battery = packets::battery_code(battery_pct);
    sent.n = detector_.fill_entries(now_ms, sent.nb);
    return packets::pack_beacon(sent, out, cap);
}

bool Node::toggle_role(uint32_t now_ms) {
    if (!role_can_toggle(now_ms, run_started_)) return false;
    role_ = role_toggled(role_);
    return true;
}

NodeStatus Node::status(uint32_t now_ms) const {
    NodeStatus s = {};
    s.id = id_;
    s.role = role_;
    s.hops = hops();
    s.heard = table_.heard(now_ms, HEARD_WINDOW_MS);
    s.run_started = run_started_;
    s.pre = detector_.pre_count();
    s.lost = detector_.lost_count();
    s.two_leaders = role_ == Role::Leader && !run_started_ && leader_seen_ &&
                    now_ms - leader_seen_ms_ <= LEADER_CONFLICT_MS;
    return s;
}
