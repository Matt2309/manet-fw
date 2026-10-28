#include "node.h"

#include <string.h>

#include "config.h"

namespace {

bool due(uint32_t now, uint32_t at) { return static_cast<int32_t>(now - at) >= 0; }

}  // namespace

// --- Utilita' ------------------------------------------------------------------------------------------------

uint32_t Node::rand_below(uint32_t n) {
    rng_ ^= rng_ << 13;  // xorshift32
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return n == 0 ? 0 : rng_ % n;
}

NodeLog* Node::new_log(NodeLog::Kind kind, uint32_t now_ms) {
    if (log_count_ == LOG_LEN) {  // piena: si perde la riga piu' vecchia
        log_head_ = (log_head_ + 1) % LOG_LEN;
        log_count_--;
    }
    NodeLog& l = log_[(log_head_ + log_count_) % LOG_LEN];
    log_count_++;
    l = NodeLog{};
    l.kind = kind;
    l.t_ms = now_ms;
    return &l;
}

bool Node::pop_log(NodeLog& item) {
    if (log_count_ == 0) return false;
    item = log_[log_head_];
    log_head_ = (log_head_ + 1) % LOG_LEN;
    log_count_--;
    return true;
}

void Node::log_tel(uint32_t now_ms, TelAction action, uint8_t id, uint16_t t_s, uint8_t peer, uint8_t tries, int32_t ms) {
    NodeLog* l = new_log(NodeLog::Kind::Tel, now_ms);
    l->u.tel = {action, id, t_s, peer, static_cast<uint16_t>(queue_.size()), tries, ms};
}

void Node::log_alm(uint32_t now_ms, AlmAction action, uint8_t creator, uint8_t seq, uint8_t runner, uint8_t type) {
    NodeLog* l = new_log(NodeLog::Kind::Alm, now_ms);
    l->u.alm = {action, creator, seq, runner, type};
}

void Node::log_cmd(uint32_t now_ms, packets::Cmd cmd, CmdOrigin origin, uint8_t seq, uint16_t elapsed_ds) {
    NodeLog* l = new_log(NodeLog::Kind::Cmd, now_ms);
    l->u.cmd = {cmd, origin, seq, elapsed_ds};
}

int Node::push_out(bool unicast, uint8_t dest, const uint8_t* data, size_t len, uint32_t due_ms) {
    for (size_t i = 0; i < OUTBOX_LEN; i++) {
        if (out_[i].used) continue;
        OutEntry& e = out_[i];
        e = OutEntry{};
        e.used = true;
        e.unicast = unicast;
        e.dest = dest;
        e.due_ms = due_ms;
        e.len = static_cast<uint16_t>(len);
        memcpy(e.data, data, len);
        return static_cast<int>(i);
    }
    return -1;  // uscita piena: il pacchetto si perde (i broadcast si ripetono, la telemetria resta in coda)
}

bool Node::pop_outgoing(uint32_t now_ms, Outgoing& o) {
    for (OutEntry& e : out_) {
        if (!e.used || !due(now_ms, e.due_ms)) continue;
        o.unicast = e.unicast;
        o.dest = e.dest;
        if (e.refresh_cmd) {
            // Il comando rilanciato porta i decimi di secondo trascorsi dall'invio originale, compreso il ritardo qui.
            packets::Command c = e.cmd;
            uint32_t elapsed = c.elapsed_ds + (now_ms - e.rx_ms) / 100;
            c.elapsed_ds = elapsed > 0xFFFF ? 0xFFFF : static_cast<uint16_t>(elapsed);
            o.len = static_cast<uint16_t>(packets::pack_command(c, o.data, sizeof(o.data)));
        } else {
            memcpy(o.data, e.data, e.len);
            o.len = e.len;
        }
        e.used = false;
        return true;
    }
    return false;
}

// --- Ricezione -----------------------------------------------------------------------------------------------

bool Node::on_packet(const uint8_t* data, size_t len, int8_t rssi, uint32_t now_ms, packets::Beacon& b) {
    packets::Type type;
    uint8_t sender;
    if (!packets::read_header(data, len, type, sender)) return false;
    if (sender == 0 || sender > MAX_NODES || sender == id_) return false;
    switch (type) {
        case packets::Type::Beacon:
            return on_beacon(data, len, rssi, now_ms, b);
        case packets::Type::Telemetry:
            on_telemetry(data, len, now_ms);
            break;
        case packets::Type::Alarm:
            on_alarm(data, len, now_ms);
            break;
        case packets::Type::Command:
            on_command(data, len, now_ms);
            break;
    }
    return false;
}

bool Node::on_beacon(const uint8_t* data, size_t len, int8_t rssi, uint32_t now_ms, packets::Beacon& b) {
    if (!packets::unpack_beacon(data, len, b)) return false;
    table_.update(b.sender, rssi, now_ms, b.hops);
    detector_.on_beacon(b, rssi, now_ms);
    if (b.hops == 0) {
        leader_seen_ = true;
        leader_seen_ms_ = now_ms;
    }
    if (role_ == Role::Leader) leader_.on_beacon(b, now_ms);
    apply_run(run_.on_beacon(b, now_ms), now_ms);
    return true;
}

void Node::on_telemetry(const uint8_t* data, size_t len, uint32_t now_ms) {
    uint8_t sender;
    size_t count;
    if (!packets::unpack_telemetry(data, len, sender, batch_, packets::MAX_TEL_RECORDS, count)) return;
    for (size_t i = 0; i < count; i++) {
        const packets::TelRecord& r = batch_[i];
        log_tel(now_ms, TelAction::Received, r.id, r.t_s, sender, 0, -1);
        if (f2_ || !run_.running()) continue;
        if (r.id == 0 || r.id > MAX_NODES || r.id == id_) continue;
        if (!seen_records_.add(r.id, r.t_s)) continue;  // doppione
        consume_record(r, now_ms);
    }
}

void Node::consume_record(const packets::TelRecord& r, uint32_t now_ms) {
    if (role_ != Role::Leader) {
        queue_.push(r);
        return;
    }
    // Il capogruppo e' la destinazione: il record aggiorna la tabella. L'eta' e' quella stimata dal capogruppo.
    uint32_t run_s = run_.seconds(now_ms);
    leader_.on_record(r, now_ms, run_s > r.t_s ? run_s - r.t_s : 0);
}

void Node::on_alarm(const uint8_t* data, size_t len, uint32_t now_ms) {
    packets::Alarm a;
    if (!packets::unpack_alarm(data, len, a)) return;
    if (!alarm_seen_.add(a.sender, a.seq)) return;
    log_alm(now_ms, AlmAction::Received, a.sender, a.seq, a.runner, static_cast<uint8_t>(a.type));
    if (!run_.running()) return;  // prima dello START e dopo lo STOP non ci sono allarmi
    if (role_ == Role::Leader) leader_.on_alarm(a, now_ms);
    // Rilancio unico, uguale all'originale (il creatore resta nell'intestazione), con un piccolo ritardo casuale.
    if (push_out(false, 0, data, len, now_ms + rand_below(RELAY_JITTER_MS + 1)) >= 0) {
        log_alm(now_ms, AlmAction::Relayed, a.sender, a.seq, a.runner, static_cast<uint8_t>(a.type));
    }
}

void Node::on_command(const uint8_t* data, size_t len, uint32_t now_ms) {
    packets::Command c;
    if (!packets::unpack_command(data, len, c)) return;
    if (!cmd_seen_.add(c.sender, c.seq)) return;
    log_cmd(now_ms, c.cmd, CmdOrigin::Radio, c.seq, c.elapsed_ds);
    apply_run(run_.on_command(c.cmd, c.elapsed_ds, now_ms), now_ms);
    int k = push_out(false, 0, data, len, now_ms + rand_below(RELAY_JITTER_MS + 1));
    if (k >= 0) {
        out_[k].refresh_cmd = true;
        out_[k].cmd = c;
        out_[k].rx_ms = now_ms;
    }
}

// --- Corsa ---------------------------------------------------------------------------------------------------

void Node::apply_run(Run::Change change, uint32_t now_ms) {
    if (change == Run::Change::Started) begin_run(now_ms, false);
    if (change == Run::Change::Stopped) end_run();
}

void Node::begin_run(uint32_t now_ms, bool local) {
    if (role_ == Role::Leader) {
        leader_.clear_alarms();
        next_state_ms_ = now_ms;
        // Un riavvio a corsa in corso riprende il file e il seq; ogni altro START apre un file nuovo e riparte da 1.
        if (local || !resume_pending_) {
            ble_seq_ = 0;
            push_ble(BleMsg::Kind::NewRun, false, nullptr, 0);
        }
        resume_pending_ = false;
    }
    queue_.clear();
    seen_records_.clear();
    memset(last_alarm_code_, 0, sizeof(last_alarm_code_));
    sos_ = false;
    inflight_.active = false;  // un eventuale esito in arrivo (awaiting_) si scarta
    next_record_ms_ = now_ms;
    next_send_ms_ = now_ms;
    last_degraded_ms_ = 0;
}

void Node::end_run() {
    if (role_ == Role::Leader) {
        leader_.reset();
        push_ble(BleMsg::Kind::EndRun, false, nullptr, 0);
    }
    queue_.clear();
    sos_ = false;
    inflight_.active = false;
}

Node::CmdResult Node::start_run(uint32_t now_ms) {
    if (role_ != Role::Leader) return CmdResult::NotLeader;
    if (!run_.start(now_ms)) return CmdResult::Already;
    resume_pending_ = false;
    begin_run(now_ms, true);
    packets::Command c = {id_, cmd_seq_++, packets::Cmd::Start, 0};
    uint8_t buf[packets::MAX_SIZE];
    push_out(false, 0, buf, packets::pack_command(c, buf, sizeof(buf)), now_ms);
    log_cmd(now_ms, c.cmd, CmdOrigin::App, c.seq, 0);
    return CmdResult::Done;
}

Node::CmdResult Node::stop_run(uint32_t now_ms) {
    if (role_ != Role::Leader) return CmdResult::NotLeader;
    if (!run_.stop()) return CmdResult::Already;
    end_run();
    packets::Command c = {id_, cmd_seq_++, packets::Cmd::Stop, 0};
    uint8_t buf[packets::MAX_SIZE];
    push_out(false, 0, buf, packets::pack_command(c, buf, sizeof(buf)), now_ms);
    log_cmd(now_ms, c.cmd, CmdOrigin::App, c.seq, 0);
    return CmdResult::Done;
}

bool Node::toggle_sos(uint32_t now_ms) {
    if (!run_.running()) return false;
    sos_ = !sos_;
    if (sos_) {
        create_alarm(id_, packets::AlarmType::Sos, 0, now_ms);
        next_sos_ms_ = now_ms + SOS_PERIOD_MS;
    }
    return true;
}

bool Node::toggle_role(uint32_t now_ms) {
    if (!role_can_toggle(now_ms, run_.running())) return false;
    role_ = role_toggled(role_);
    if (role_ == Role::Leader) leader_.set_self(id_);
    hops_ = compute_hops(now_ms);
    return true;
}

// --- Salti e candidati ---------------------------------------------------------------------------------------

uint8_t Node::compute_hops(uint32_t now_ms) const {
    if (role_ == Role::Leader) return 0;
    uint8_t best = packets::HOPS_ISOLATED;
    for (size_t i = 0; i < table_.size(); i++) {
        const Neighbor& nb = table_.at(i);
        if (now_ms - nb.last_ms > HEARD_WINDOW_MS || nb.hops >= packets::HOPS_ISOLATED - 1) continue;
        LinkView v;
        if (!detector_.estimate(nb.id, now_ms, v) || v.r <= ROUTE_MIN_DBM) continue;
        if (nb.hops + 1 < best) best = static_cast<uint8_t>(nb.hops + 1);
    }
    return best;
}

// I due compagni con meno salti di me, sentiti da poco e con stima sopra la soglia, dal segnale piu' forte.
size_t Node::candidates(uint32_t now_ms, uint8_t out[2]) const {
    double signal[2] = {0.0, 0.0};
    size_t n = 0;
    for (size_t i = 0; i < table_.size(); i++) {
        const Neighbor& nb = table_.at(i);
        if (now_ms - nb.last_ms > HEARD_WINDOW_MS || nb.hops >= hops_) continue;
        LinkView v;
        if (!detector_.estimate(nb.id, now_ms, v) || v.r <= ROUTE_MIN_DBM) continue;
        if (n < 2) {
            out[n] = nb.id;
            signal[n++] = v.r;
        } else if (v.r > signal[1]) {
            out[1] = nb.id;
            signal[1] = v.r;
        } else {
            continue;
        }
        if (n == 2 && signal[1] > signal[0]) {
            uint8_t id = out[0];
            double s = signal[0];
            out[0] = out[1];
            signal[0] = signal[1];
            out[1] = id;
            signal[1] = s;
        }
    }
    return n;
}

// --- Beacon --------------------------------------------------------------------------------------------------

size_t Node::build_beacon(uint32_t now_ms, int battery_pct, uint8_t* out, size_t cap, packets::Beacon& sent) {
    battery_pct_ = battery_pct;
    sent = packets::Beacon{};
    sent.sender = id_;
    sent.seq = seq_++;
    sent.hops = hops_;
    sent.t_s = run_.seconds(now_ms);
    sent.stopped = run_.state() == RunState::Stopped;
    sent.sos = sos_;
    sent.has_pos = detector_.own_position(now_ms, sent.pos);
    sent.battery = packets::battery_code(battery_pct);
    sent.n = detector_.fill_entries(now_ms, sent.nb);
    return packets::pack_beacon(sent, out, cap);
}

// --- Telemetria ----------------------------------------------------------------------------------------------

void Node::produce_record(uint32_t now_ms) {
    packets::TelRecord r = {};
    r.id = id_;
    r.t_s = run_.seconds(now_ms);
    r.battery = packets::battery_code(battery_pct_);
    r.sos = sos_;
    r.isolated = role_ != Role::Leader && hops_ == packets::HOPS_ISOLATED;
    r.has_pos = detector_.own_position(now_ms, r.pos);
    for (uint8_t id = 1; id <= MAX_NODES; id++) {
        if (r.n < packets::MAX_NEIGHBORS && detector_.report(id, now_ms, r.nb[r.n])) r.n++;
    }
    log_tel(now_ms, TelAction::Produced, r.id, r.t_s, 0, 0, -1);

    if (role_ == Role::Leader) {
        consume_record(r, now_ms);  // il capogruppo e' la destinazione
        return;
    }
    // Coda piena: un record proprio ogni TEL_DEGRADED_PERIOD_MS (un SOS passa sempre).
    if (queue_.full() && !r.sos) {
        if (!due(now_ms, last_degraded_ms_ + TEL_DEGRADED_PERIOD_MS)) return;
        last_degraded_ms_ = now_ms;
    }
    queue_.push(r);
}

void Node::pump_uplink(uint32_t now_ms) {
    if (role_ == Role::Leader || inflight_.active || awaiting_ || queue_.empty()) return;
    if (!due(now_ms, next_send_ms_)) return;

    uint8_t cands[2];
    size_t n_cands = candidates(now_ms, cands);
    if (n_cands == 0) {  // nessuna via: i record restano in coda
        if (due(now_ms, next_queue_log_ms_)) {
            next_queue_log_ms_ = now_ms + TEL_QUEUE_LOG_MS;
            log_tel(now_ms, TelAction::Queued, 0, 0, 0, 0, -1);
        }
        return;
    }

    uint32_t first_serial, last_serial;
    size_t count = queue_.peek(batch_, packets::MAX_TEL_RECORDS, first_serial, last_serial);
    inflight_ = Inflight{};
    size_t packed = 0;
    size_t len = packets::pack_telemetry(id_, batch_, count, inflight_.data, sizeof(inflight_.data), packed);
    if (len == 0) {  // un record che non entra in un pacchetto non uscirebbe mai
        queue_.drop_through(first_serial);
        return;
    }
    inflight_.active = true;
    inflight_.dest = cands[0];
    inflight_.cands[0] = cands[0];
    inflight_.cands[1] = n_cands > 1 ? cands[1] : 0;
    inflight_.n_cands = static_cast<uint8_t>(n_cands);
    inflight_.attempt = 1;
    inflight_.packed = packed;
    inflight_.last_serial = queue_.serial_at(packed - 1);
    inflight_.first_id = batch_[0].id;
    inflight_.first_t_s = batch_[0].t_s;
    inflight_.len = static_cast<uint16_t>(len);
    send_attempt(now_ms);
}

void Node::send_attempt(uint32_t now_ms) {
    push_out(true, inflight_.dest, inflight_.data, inflight_.len, now_ms);
    awaiting_ = true;
    sent_ms_ = now_ms;
    log_tel(now_ms, TelAction::Sent, inflight_.first_id, inflight_.first_t_s, inflight_.dest, inflight_.attempt, -1);
}

void Node::on_send_result(bool ok, uint32_t now_ms) {
    if (!awaiting_) return;
    awaiting_ = false;
    if (!inflight_.active) return;  // la corsa e' finita o ripartita nel frattempo
    int32_t ms = static_cast<int32_t>(now_ms - sent_ms_);
    if (ok) {
        log_tel(now_ms, TelAction::Confirmed, inflight_.first_id, inflight_.first_t_s, inflight_.dest, inflight_.attempt, ms);
        if (!inflight_.f2) queue_.drop_through(inflight_.last_serial);
        inflight_.active = false;
        next_send_ms_ = now_ms + TEL_MIN_GAP_MS;
        return;
    }
    log_tel(now_ms, TelAction::Failed, inflight_.first_id, inflight_.first_t_s, inflight_.dest, inflight_.attempt, ms);
    if (inflight_.attempt < TEL_ATTEMPTS) {  // ancora sullo stesso compagno
        inflight_.attempt++;
        send_attempt(now_ms);
    } else if (!inflight_.f2 && inflight_.cand_idx + 1u < inflight_.n_cands) {  // poi sul secondo
        inflight_.cand_idx++;
        inflight_.dest = inflight_.cands[inflight_.cand_idx];
        inflight_.attempt = 1;
        send_attempt(now_ms);
    } else {  // tutte le vie provate: si riscegliera' fra TEL_RETRY_MS
        inflight_.active = false;
        next_send_ms_ = now_ms + TEL_RETRY_MS;
    }
}

// Prova F2: un unicast da circa 223 byte al secondo, a rotazione, con conferma e tentativi registrati.
void Node::f2_tick(uint32_t now_ms) {
    if (inflight_.active || awaiting_ || !due(now_ms, next_f2_ms_)) return;
    uint8_t dest = 0, first = 0;
    for (size_t i = 0; i < table_.size(); i++) {
        uint8_t id = table_.at(i).id;
        if (id > last_f2_dest_ && (dest == 0 || id < dest)) dest = id;
        if (first == 0 || id < first) first = id;
    }
    if (dest == 0) dest = first;
    if (dest == 0) return;  // nessun compagno noto
    next_f2_ms_ = now_ms + F2_PERIOD_MS;
    last_f2_dest_ = dest;

    packets::TelRecord rec[F2_RECORDS] = {};
    for (int k = 0; k < F2_RECORDS; k++) {
        rec[k].id = id_;
        rec[k].t_s = static_cast<uint16_t>(now_ms / 1000);
        rec[k].has_pos = true;
        rec[k].n = packets::MAX_NEIGHBORS;
        for (uint8_t j = 0; j < rec[k].n; j++) rec[k].nb[j] = packets::TelEntry{static_cast<uint8_t>(j + 1), -70, 20, 3, 0};
    }
    inflight_ = Inflight{};
    size_t packed = 0;
    inflight_.len = static_cast<uint16_t>(packets::pack_telemetry(id_, rec, F2_RECORDS, inflight_.data, sizeof(inflight_.data), packed));
    inflight_.active = true;
    inflight_.f2 = true;
    inflight_.dest = dest;
    inflight_.attempt = 1;
    inflight_.first_id = id_;
    inflight_.first_t_s = rec[0].t_s;
    send_attempt(now_ms);
}

// --- Allarmi -------------------------------------------------------------------------------------------------

void Node::create_alarm(uint8_t runner, packets::AlarmType type, uint8_t signal, uint32_t now_ms) {
    packets::Alarm a = {};
    a.sender = id_;
    a.seq = alarm_seq_++;
    a.runner = runner;
    a.type = type;
    uint32_t last_ms;
    if (runner != id_ && detector_.last_contact(runner, last_ms)) {
        int32_t since_t0 = static_cast<int32_t>(last_ms - run_.t0());
        a.last_t_s = since_t0 < 0 ? 0 : static_cast<uint16_t>(since_t0 / 1000);
    } else {
        a.last_t_s = run_.seconds(now_ms);
    }
    a.last_signal = static_cast<int8_t>(signal);
    a.has_pos = detector_.own_position(now_ms, a.pos);
    uint8_t buf[packets::MAX_SIZE];
    push_out(false, 0, buf, packets::pack_alarm(a, buf, sizeof(buf)), now_ms);
    log_alm(now_ms, AlmAction::Created, a.sender, a.seq, a.runner, static_cast<uint8_t>(a.type));
}

// A ogni cambio dell'allarme di un compagno (pre-allarme, perso, fine) parte un allarme a tutti.
void Node::track_alarms(uint32_t now_ms) {
    if (role_ == Role::Leader) return;  // il capogruppo usa le proprie osservazioni direttamente
    for (uint8_t id = 1; id <= MAX_NODES; id++) {
        uint8_t code = detector_.alarm_code(id);
        if (code == last_alarm_code_[id]) continue;
        last_alarm_code_[id] = code;
        packets::AlarmType type = code == packets::ALARM_LOST  ? packets::AlarmType::Lost
                                  : code == packets::ALARM_PRE ? packets::AlarmType::PreAlarm
                                                               : packets::AlarmType::End;
        packets::TelEntry e;
        create_alarm(id, type, detector_.report(id, now_ms, e) ? static_cast<uint8_t>(e.signal) : 0, now_ms);
    }
}

// --- Capogruppo e BLE ----------------------------------------------------------------------------------------

bool Node::pop_ble(BleMsg& m) {
    if (ble_count_ == 0) return false;
    m = ble_[ble_head_];
    ble_head_ = (ble_head_ + 1) % BLE_QUEUE_LEN;
    ble_count_--;
    return true;
}

void Node::push_ble(BleMsg::Kind kind, bool save, const uint8_t* data, size_t len) {
    if (ble_count_ == BLE_QUEUE_LEN) {  // piena: si perde il messaggio piu' vecchio (un STATO si rifa' fra poco)
        ble_head_ = (ble_head_ + 1) % BLE_QUEUE_LEN;
        ble_count_--;
    }
    BleMsg& m = ble_[(ble_head_ + ble_count_) % BLE_QUEUE_LEN];
    ble_count_++;
    m = BleMsg{};
    m.kind = kind;
    m.save = save;
    m.len = static_cast<uint16_t>(len);
    if (len > 0) memcpy(m.data, data, len);
}

void Node::resume_ble_seq(uint32_t last_seq) {
    ble_seq_ = last_seq;
    resume_pending_ = true;
}

// STATO: con la corsa avviata ha seq e secondi da T0 e si salva; altrimenti seq 0 e t riservato (vista live, non salvata).
void Node::push_state(uint32_t now_ms) {
    ble::Row rows[ble::MAX_RUNNERS];
    size_t n = leader_.snapshot(now_ms, rows, ble::MAX_RUNNERS);
    uint8_t buf[ble::MAX_MESSAGE_SIZE];
    size_t len;
    bool running = run_.running();
    if (running) {
        len = ble::encode_state(++ble_seq_, (now_ms - run_.t0()) / 1000, rows, n, buf, sizeof(buf));
    } else {
        len = ble::encode_state(0, ble::T_NOT_STARTED, rows, n, buf, sizeof(buf));
    }
    if (len > 0) push_ble(BleMsg::Kind::Data, running, buf, len);
}

void Node::lead_tick(uint32_t now_ms) {
    if (due(now_ms, next_self_ms_)) {
        next_self_ms_ = now_ms + 1000;
        leader_.on_self(static_cast<uint8_t>(battery_pct_), sos_, now_ms);
    }
    if (run_.running()) {
        Leader::Event ev[Leader::MAX_EVENTS];
        size_t n = leader_.tick(now_ms, (now_ms - run_.t0()) / 1000, ev);
        for (size_t i = 0; i < n; i++) {
            uint8_t buf[ble::EVENT_SIZE];
            size_t len = ble::encode_event(++ble_seq_, ev[i].t_s, ev[i].id, ev[i].type, buf, sizeof(buf));
            if (len > 0) push_ble(BleMsg::Kind::Urgent, true, buf, len);
        }
    }
    if (due(now_ms, next_state_ms_)) {
        next_state_ms_ = now_ms + STATE_PERIOD_MS;
        push_state(now_ms);
    }
}

void Node::on_app_command(const ble::Command& c, uint32_t now_ms) {
    uint8_t buf[ble::CONFIRM_RESUME_SIZE];
    bool leader = role_ == Role::Leader;
    switch (c.code) {
        case ble::CommandCode::Start:
        case ble::CommandCode::Stop: {
            // Idempotenti: un START a corsa avviata (o uno STOP a corsa ferma) e' confermato senza cambiare nulla.
            bool is_start = c.code == ble::CommandCode::Start;
            CmdResult r = is_start ? start_run(now_ms) : stop_run(now_ms);
            push_ble(BleMsg::Kind::Data, false, buf, ble::encode_confirm(c.code, r != CmdResult::NotLeader, buf, sizeof(buf)));
            if (is_start && r == CmdResult::Done) next_state_ms_ = now_ms;  // lo STATO con seq 1 parte subito
            break;
        }
        case ble::CommandCode::CloseAlarm:
            push_ble(BleMsg::Kind::Data, false, buf,
                     ble::encode_confirm(c.code, leader && leader_.close_alarm(c.id, c.kind, now_ms), buf, sizeof(buf)));
            break;
        case ble::CommandCode::ResumeFrom:
            // La CONFERMA porta l'ultimo seq disponibile; poi si rimandano in ordine i messaggi successivi.
            push_ble(BleMsg::Kind::Data, false, buf, ble::encode_confirm_resume(leader, ble_seq_, buf, sizeof(buf)));
            if (leader && ble_seq_ > c.last_seq) {
                push_ble(BleMsg::Kind::Resend, false, nullptr, 0);
                BleMsg& m = ble_[(ble_head_ + ble_count_ - 1) % BLE_QUEUE_LEN];
                m.from_seq = c.last_seq;
                m.to_seq = ble_seq_;
            }
            break;
    }
}

// --- Controlli a tempo ---------------------------------------------------------------------------------------

size_t Node::tick(uint32_t now_ms, DetEvent* det) {
    size_t n = detector_.tick(now_ms, det);
    hops_ = compute_hops(now_ms);
    if (awaiting_ && now_ms - sent_ms_ > TEL_RESULT_TIMEOUT_MS) on_send_result(false, now_ms);
    if (f2_) {
        f2_tick(now_ms);
        return n;
    }
    if (role_ == Role::Leader) lead_tick(now_ms);
    if (!run_.running()) return n;

    if (due(now_ms, next_record_ms_)) {
        produce_record(now_ms);
        next_record_ms_ += TEL_PERIOD_MS;
        if (due(now_ms, next_record_ms_)) next_record_ms_ = now_ms + TEL_PERIOD_MS;
    }
    track_alarms(now_ms);
    if (sos_ && due(now_ms, next_sos_ms_)) {
        create_alarm(id_, packets::AlarmType::Sos, 0, now_ms);
        next_sos_ms_ = now_ms + SOS_PERIOD_MS;
    }
    pump_uplink(now_ms);
    return n;
}

NodeStatus Node::status(uint32_t now_ms) const {
    NodeStatus s = {};
    s.id = id_;
    s.role = role_;
    s.hops = hops_;
    s.heard = table_.heard(now_ms, HEARD_WINDOW_MS);
    s.run_started = run_.running();
    s.run = run_.state();
    s.run_s = run_.state() == RunState::NotStarted ? 0 : (now_ms - run_.t0()) / 1000;
    s.sos = sos_;
    s.isolated = run_.running() && role_ != Role::Leader && hops_ == packets::HOPS_ISOLATED;
    s.pre = detector_.pre_count();
    s.lost = detector_.lost_count();
    s.group = role_ == Role::Leader ? static_cast<uint8_t>(leader_.size()) : 0;
    s.two_leaders = role_ == Role::Leader && !run_.running() && leader_seen_ &&
                    now_ms - leader_seen_ms_ <= LEADER_CONFLICT_MS;
    return s;
}
