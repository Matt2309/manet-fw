// Rete di Node in memoria: radio simulata (broadcast a chi e' in portata, unicast con esito), nessun hardware.
// Catena di default 1 - 2 - 3: il capogruppo (1) e il 3 non si sentono, tutto passa dal 2. La 4 e' isolata salvo diversa indicazione.
#include <stdio.h>
#include <unity.h>

#include <functional>
#include <memory>
#include <set>
#include <vector>

#include "config.h"
#include "node.h"

void setUp() {}
void tearDown() {}

namespace {

constexpr int N = 4;
constexpr int CHAIN = 3;  // le schede della catena 1 - 2 - 3 (la 4 e' fuori salvo diversa indicazione)
constexpr uint32_t STEP_MS = 50;

struct Net {
    std::unique_ptr<Node> node[N + 1];
    bool link[N + 1][N + 1] = {};  // link[a][b]: b sente a
    uint32_t now = 1000;
    uint32_t next_beacon[N + 1] = {};
    int cmd_packets = 0, alarm_packets = 0;
    std::vector<NodeLog> logs[N + 1];
    std::vector<BleMsg> ble;  // messaggi BLE del capogruppo
    std::function<bool(int from, int to)> ack_ok = [](int, int) { return true; };

    Net() {
        for (int i = 1; i <= N; i++) {
            node[i].reset(new Node(static_cast<uint8_t>(i), i == 1 ? Role::Leader : Role::Runner));
            node[i]->seed(1000u + i);
            next_beacon[i] = now + static_cast<uint32_t>(i) * 130;
        }
        set_link(1, 2, true);
        set_link(2, 3, true);
    }

    void set_link(int a, int b, bool up) { link[a][b] = link[b][a] = up; }

    void deliver(int to, const uint8_t* data, size_t len) {
        packets::Beacon b;
        node[to]->on_packet(data, len, -60, now, b);
    }

    void drain(int i) {
        Outgoing o;
        while (node[i]->pop_outgoing(now, o)) {
            if (o.unicast) {
                bool in_range = link[i][o.dest];
                if (in_range) deliver(o.dest, o.data, o.len);
                node[i]->on_send_result(in_range && ack_ok(i, o.dest), now);
            } else {
                if (o.data[2] == static_cast<uint8_t>(packets::Type::Command)) cmd_packets++;
                if (o.data[2] == static_cast<uint8_t>(packets::Type::Alarm)) alarm_packets++;
                for (int to = 1; to <= N; to++) {
                    if (link[i][to]) deliver(to, o.data, o.len);
                }
            }
        }
        NodeLog l;
        while (node[i]->pop_log(l)) logs[i].push_back(l);
        BleMsg m;
        while (node[i]->pop_ble(m)) {
            if (i == 1) ble.push_back(m);
        }
    }

    void step() {
        now += STEP_MS;
        for (int i = 1; i <= N; i++) {
            if (!due(next_beacon[i])) continue;
            next_beacon[i] += BEACON_PERIOD_MS;
            uint8_t buf[packets::MAX_SIZE];
            packets::Beacon sent;
            size_t len = node[i]->build_beacon(now, 80, buf, sizeof(buf), sent);
            for (int to = 1; to <= N; to++) {
                if (link[i][to]) deliver(to, buf, len);
            }
        }
        if (now % LOGIC_TICK_MS == 0) {
            for (int i = 1; i <= N; i++) {
                DetEvent ev[Detector::MAX_EVENTS];
                node[i]->tick(now, ev);
            }
        }
        for (int i = 1; i <= N; i++) drain(i);
    }

    bool due(uint32_t at) const { return static_cast<int32_t>(now - at) >= 0; }
    void run(uint32_t ms) {
        for (uint32_t t = 0; t < ms; t += STEP_MS) step();
    }

    // Secondi da T0 dei record della scheda origin che il ricevente ha loggato come ricevuti da forwarder (0 = qualsiasi).
    std::vector<uint16_t> received(int at, int origin, int forwarder = 0) const {
        std::vector<uint16_t> v;
        for (const NodeLog& l : logs[at]) {
            if (l.kind == NodeLog::Kind::Tel && l.u.tel.action == TelAction::Received && l.u.tel.id == origin &&
                (forwarder == 0 || l.u.tel.peer == forwarder)) {
                v.push_back(l.u.tel.t_s);
            }
        }
        return v;
    }
    int count_alm(int at, AlmAction a) const {
        int n = 0;
        for (const NodeLog& l : logs[at]) {
            if (l.kind == NodeLog::Kind::Alm && l.u.alm.action == a) n++;
        }
        return n;
    }
    NodeStatus st(int i) { return node[i]->status(now); }

    // Messaggi BLE di un tipo (data[1]) e, per lo STATO, dei corridori.
    std::vector<BleMsg> msgs(uint8_t type) const {
        std::vector<BleMsg> v;
        for (const BleMsg& m : ble) {
            if ((m.kind == BleMsg::Kind::Data || m.kind == BleMsg::Kind::Urgent) && m.len > 1 && m.data[1] == type) v.push_back(m);
        }
        return v;
    }
    int count_kind(BleMsg::Kind k) const {
        int n = 0;
        for (const BleMsg& m : ble) n += m.kind == k ? 1 : 0;
        return n;
    }
};

uint32_t rd32(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
// Riga del corridore id in uno STATO: stato, R, batteria (puntatore alla riga, nullptr se manca).
const uint8_t* state_row(const BleMsg& m, uint8_t id) {
    for (int i = 0; i < m.data[10]; i++) {
        const uint8_t* r = m.data + 11 + 6 * i;
        if (r[0] == id) return r;
    }
    return nullptr;
}

size_t unique_count(const std::vector<uint16_t>& v) { return std::set<uint16_t>(v.begin(), v.end()).size(); }

}  // namespace

void test_salti_lungo_la_catena() {
    Net net;
    net.run(6000);
    TEST_ASSERT_EQUAL(0, net.st(1).hops);
    TEST_ASSERT_EQUAL(1, net.st(2).hops);
    TEST_ASSERT_EQUAL(2, net.st(3).hops);
}

void test_start_si_propaga_e_si_rilancia_una_volta() {
    Net net;
    net.run(6000);
    TEST_ASSERT_EQUAL(Node::CmdResult::NotLeader, net.node[2]->start_run(net.now));
    TEST_ASSERT_EQUAL(Node::CmdResult::Done, net.node[1]->start_run(net.now));
    net.run(3000);
    for (int i = 1; i <= CHAIN; i++) TEST_ASSERT_TRUE(net.st(i).run_started);
    // Capogruppo, 2 e 3 mandano il comando una volta ciascuno; nessun altro rilancio.
    TEST_ASSERT_EQUAL(3, net.cmd_packets);
    // Tutti contano gli stessi secondi, a meno dell'arrotondamento.
    TEST_ASSERT_UINT32_WITHIN(1, net.st(1).run_s, net.st(2).run_s);
    TEST_ASSERT_UINT32_WITHIN(1, net.st(1).run_s, net.st(3).run_s);
    // START ripetuto: confermato ma nessun nuovo pacchetto, T0 invariato.
    TEST_ASSERT_EQUAL(Node::CmdResult::Already, net.node[1]->start_run(net.now));
    net.run(1000);
    TEST_ASSERT_EQUAL(3, net.cmd_packets);
}

void test_stop_si_propaga() {
    Net net;
    net.run(6000);
    net.node[1]->start_run(net.now);
    net.run(5000);
    TEST_ASSERT_EQUAL(Node::CmdResult::Done, net.node[1]->stop_run(net.now));
    net.run(2000);
    for (int i = 1; i <= CHAIN; i++) TEST_ASSERT_EQUAL(RunState::Stopped, net.st(i).run);
    TEST_ASSERT_EQUAL(6, net.cmd_packets);
    TEST_ASSERT_EQUAL(Node::CmdResult::Already, net.node[1]->stop_run(net.now));
    TEST_ASSERT_EQUAL(0, net.node[3]->queue_size());
}

void test_telemetria_a_due_salti_arriva_al_capogruppo() {
    Net net;
    net.run(6000);
    net.node[1]->start_run(net.now);
    net.run(20000);
    std::vector<uint16_t> from3 = net.received(1, 3), from2 = net.received(1, 2);
    TEST_ASSERT_TRUE(unique_count(from3) >= 17);
    TEST_ASSERT_TRUE(unique_count(from2) >= 17);
    TEST_ASSERT_TRUE(net.node[3]->queue_size() <= 3);
    TEST_ASSERT_TRUE(net.node[2]->queue_size() <= 3);
    // Il capogruppo non rimanda indietro nulla: la sua coda e' vuota.
    TEST_ASSERT_EQUAL(0, net.node[1]->queue_size());
}

void test_partizione_i_record_restano_in_coda_e_poi_arrivano_senza_doppioni() {
    Net net;
    net.run(6000);
    net.node[1]->start_run(net.now);
    net.run(5000);
    net.set_link(2, 3, false);
    net.run(40000);
    TEST_ASSERT_TRUE(net.node[3]->queue_size() >= 35);
    TEST_ASSERT_EQUAL(packets::HOPS_ISOLATED, net.st(3).hops);
    TEST_ASSERT_TRUE(net.st(3).isolated);
    net.set_link(2, 3, true);
    net.run(30000);
    TEST_ASSERT_TRUE(net.node[3]->queue_size() <= 3);
    std::vector<uint16_t> from3 = net.received(1, 3);
    // Arrivano tutti i secondi della corsa, nessuno due volte.
    TEST_ASSERT_TRUE(unique_count(from3) >= 70);
    TEST_ASSERT_EQUAL(unique_count(from3), from3.size());
    TEST_ASSERT_FALSE(net.st(3).isolated);
}

void test_conferme_perse_rimandano_senza_doppioni_a_valle() {
    Net net;
    // Il pacchetto del 3 arriva al 2, ma ogni secondo esito di conferma e' perso: il 3 riprova e il 2 riceve due volte.
    int n = 0;
    net.ack_ok = [&n](int from, int) { return from != 3 || (++n % 2) == 0; };
    net.run(6000);
    net.node[1]->start_run(net.now);
    net.run(25000);
    std::vector<uint16_t> at2 = net.received(2, 3);
    std::vector<uint16_t> fwd = net.received(1, 3, 2);  // arrivati al capogruppo, inoltrati dal 2
    TEST_ASSERT_TRUE(at2.size() > unique_count(at2));    // il 2 ha davvero ricevuto dei doppioni...
    TEST_ASSERT_EQUAL(unique_count(fwd), fwd.size());    // ...ma non li ha inoltrati
    TEST_ASSERT_TRUE(unique_count(fwd) >= 18);
}

void test_allarme_sos_rilanciato_una_volta_e_ripetuto() {
    Net net;
    net.run(6000);
    TEST_ASSERT_FALSE(net.node[3]->toggle_sos(net.now));  // prima dello START non si puo'
    net.node[1]->start_run(net.now);
    net.run(3000);
    TEST_ASSERT_TRUE(net.node[3]->toggle_sos(net.now));
    net.run(1000);
    // Creato dal 3, rilanciato dal 2 e dal 1: tre pacchetti, nessuno di piu'.
    TEST_ASSERT_EQUAL(3, net.alarm_packets);
    TEST_ASSERT_EQUAL(1, net.count_alm(3, AlmAction::Created));
    TEST_ASSERT_EQUAL(1, net.count_alm(2, AlmAction::Received));
    TEST_ASSERT_EQUAL(1, net.count_alm(2, AlmAction::Relayed));
    TEST_ASSERT_EQUAL(1, net.count_alm(1, AlmAction::Received));
    // Si ripete ogni SOS_PERIOD_MS con un numero nuovo, e si rilancia di nuovo una volta.
    net.run(SOS_PERIOD_MS);
    TEST_ASSERT_EQUAL(6, net.alarm_packets);
    TEST_ASSERT_TRUE(net.st(3).sos);
    // Un'altra pressione lunga lo annulla: niente piu' ripetizioni.
    TEST_ASSERT_TRUE(net.node[3]->toggle_sos(net.now));
    TEST_ASSERT_FALSE(net.st(3).sos);
    net.run(3 * SOS_PERIOD_MS);
    TEST_ASSERT_EQUAL(6, net.alarm_packets);
}

void test_chi_ha_perso_lo_start_prende_t0_dai_beacon() {
    Net net;
    net.set_link(2, 3, false);  // il 3 non sente nessuno
    net.run(6000);
    net.node[1]->start_run(net.now);
    net.run(8000);
    TEST_ASSERT_FALSE(net.st(3).run_started);
    net.set_link(2, 3, true);
    net.run(2000);
    TEST_ASSERT_TRUE(net.st(3).run_started);
    TEST_ASSERT_UINT32_WITHIN(1, net.st(1).run_s, net.st(3).run_s);
}

void test_chi_ha_perso_lo_stop_lo_prende_dai_beacon() {
    Net net;
    net.run(6000);
    net.node[1]->start_run(net.now);
    net.run(5000);
    net.set_link(2, 3, false);
    net.node[1]->stop_run(net.now);
    net.run(6000);
    TEST_ASSERT_EQUAL(RunState::Running, net.st(3).run);
    net.set_link(2, 3, true);
    net.run(2000);
    TEST_ASSERT_EQUAL(RunState::Stopped, net.st(3).run);
}

void test_la_coda_si_ferma_a_600_record() {
    Net net;
    net.run(6000);
    net.node[1]->start_run(net.now);
    net.run(3000);
    net.set_link(2, 3, false);
    net.run(11 * 60 * 1000);
    TEST_ASSERT_EQUAL(RecordQueue::CAPACITY, net.node[3]->queue_size());
}

// Prova F2: un unicast da ~223 byte al secondo a rotazione, con tentativi e tempo di conferma nel registro.
static std::vector<NodeLog> tel_rows(const Net& net, int at, TelAction a) {
    std::vector<NodeLog> v;
    for (const NodeLog& l : net.logs[at]) {
        if (l.kind == NodeLog::Kind::Tel && l.u.tel.action == a) v.push_back(l);
    }
    return v;
}

void test_f2_unicast_a_rotazione_con_conferma() {
    Net net;
    net.set_link(1, 3, true);  // tutti si sentono: la rotazione tocca due compagni
    for (int i = 1; i <= N; i++) net.node[i]->set_f2(true);
    net.run(6000);
    net.run(20000);
    std::vector<NodeLog> sent = tel_rows(net, 1, TelAction::Sent), ok = tel_rows(net, 1, TelAction::Confirmed);
    TEST_ASSERT_TRUE(sent.size() >= 18);
    TEST_ASSERT_EQUAL(sent.size(), ok.size());
    TEST_ASSERT_EQUAL(1, ok[0].u.tel.tries);
    TEST_ASSERT_TRUE(ok[0].u.tel.ms >= 0);
    // A rotazione: il capogruppo manda ora al 2 ora al 3.
    std::set<int> dests;
    for (const NodeLog& l : sent) dests.insert(l.u.tel.peer);
    TEST_ASSERT_EQUAL(2, dests.size());
    // Il ricevente conta i pacchetti (2 record per pacchetto).
    TEST_ASSERT_TRUE(net.received(2, 1).size() >= 15);
}

void test_f2_tre_tentativi_poi_basta() {
    Net net;
    for (int i = 1; i <= N; i++) net.node[i]->set_f2(true);
    // Dal 3 al 2 nessuna conferma: il pacchetto arriva ma l'esito e' sempre negativo.
    net.ack_ok = [](int from, int) { return from != 3; };
    net.run(6000);
    net.run(10000);
    std::vector<NodeLog> failed = tel_rows(net, 3, TelAction::Failed), ok = tel_rows(net, 3, TelAction::Confirmed);
    TEST_ASSERT_EQUAL(0, ok.size());
    TEST_ASSERT_TRUE(failed.size() >= 6);
    TEST_ASSERT_EQUAL(1, failed[0].u.tel.tries);
    TEST_ASSERT_EQUAL(2, failed[1].u.tel.tries);
    TEST_ASSERT_EQUAL(3, failed[2].u.tel.tries);     // tre tentativi...
    TEST_ASSERT_EQUAL(1, failed[3].u.tel.tries);     // ...e poi al secondo successivo si ricomincia, senza un secondo candidato
}

// --- Capogruppo e BLE -----------------------------------------------------------------------------------------

void test_stato_prima_dello_start_ha_seq_zero_e_non_si_salva() {
    Net net;
    net.run(12000);
    std::vector<BleMsg> st = net.msgs(0x01);
    TEST_ASSERT_TRUE(st.size() >= 2);
    for (const BleMsg& m : st) {
        TEST_ASSERT_EQUAL_UINT32(0, rd32(m.data + 2));
        TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, rd32(m.data + 6));
        TEST_ASSERT_FALSE(m.save);
    }
    // Il capogruppo e il 2 (sentito); il 3 non ancora.
    TEST_ASSERT_EQUAL(2, st.back().data[10]);
}

void test_stato_dopo_lo_start_numera_salva_e_riempie_la_tabella() {
    Net net;
    net.run(6000);
    net.node[1]->start_run(net.now);
    net.run(20000);
    std::vector<BleMsg> st = net.msgs(0x01);
    std::vector<uint32_t> seqs;
    for (const BleMsg& m : st) {
        if (rd32(m.data + 2) > 0) {
            seqs.push_back(rd32(m.data + 2));
            TEST_ASSERT_TRUE(m.save);
        }
    }
    TEST_ASSERT_TRUE(seqs.size() >= 3);
    TEST_ASSERT_EQUAL_UINT32(1, seqs[0]);                       // riparte da 1
    for (size_t i = 1; i < seqs.size(); i++) TEST_ASSERT_EQUAL_UINT32(seqs[i - 1] + 1, seqs[i]);
    const BleMsg& last = st.back();
    TEST_ASSERT_TRUE(rd32(last.data + 6) >= 14);                 // secondi da T0
    TEST_ASSERT_EQUAL(3, last.data[10]);                         // capogruppo, 2 e 3 (il 3 via telemetria)
    const uint8_t* r3 = state_row(last, 3);
    TEST_ASSERT_NOT_NULL(r3);
    TEST_ASSERT_EQUAL(0, r3[1]);                                 // ok
    TEST_ASSERT_TRUE(static_cast<int8_t>(r3[2]) >= -64 && static_cast<int8_t>(r3[2]) <= -56);  // R vicino a -60
    TEST_ASSERT_EQUAL(80, r3[3]);                                // batteria
}

void test_nuovo_file_a_ogni_start_e_seq_che_riparte() {
    Net net;
    net.run(6000);
    net.node[1]->start_run(net.now);
    net.run(8000);
    TEST_ASSERT_EQUAL(1, net.count_kind(BleMsg::Kind::NewRun));
    net.node[1]->stop_run(net.now);
    net.run(2000);
    TEST_ASSERT_EQUAL(1, net.count_kind(BleMsg::Kind::EndRun));
    net.ble.clear();
    net.node[1]->start_run(net.now);
    net.run(7000);
    TEST_ASSERT_EQUAL(1, net.count_kind(BleMsg::Kind::NewRun));
    std::vector<BleMsg> st = net.msgs(0x01);
    TEST_ASSERT_EQUAL_UINT32(1, rd32(st[0].data + 2));           // di nuovo da 1
}

// Rete 1-2, 1-4, 2-3, 4-3: il 3 e' sentito da due osservatori, 2 e 4, che il capogruppo sente.
static void diamond(Net& net) {
    net.set_link(1, 4, true);
    net.set_link(4, 3, true);
}

void test_perso_confermato_da_due_osservatori_evento_e_rientro() {
    Net net;
    diamond(net);
    net.run(6000);
    net.node[1]->start_run(net.now);
    net.run(10000);
    net.set_link(2, 3, false);
    net.set_link(4, 3, false);
    net.run(20000);
    std::vector<BleMsg> ev = net.msgs(0x02);
    int lost = 0;
    for (const BleMsg& m : ev) lost += (m.data[10] == 3 && m.data[11] == 1) ? 1 : 0;   // id 3, PERSO
    TEST_ASSERT_EQUAL(1, lost);
    const uint8_t* r3 = state_row(net.msgs(0x01).back(), 3);
    TEST_ASSERT_NOT_NULL(r3);
    TEST_ASSERT_EQUAL(2, r3[1]);                                 // perso
    // Gli EVENTO hanno seq progressivi condivisi con gli STATO (mai ripetuti).
    std::set<uint32_t> seqs;
    for (const BleMsg& m : net.ble) {
        if (m.save) TEST_ASSERT_TRUE(seqs.insert(rd32(m.data + 2)).second);
    }
    // Torna: dopo 30 s di ricezione buona l'allarme si spegne da solo e arriva il RIENTRO.
    net.set_link(2, 3, true);
    net.set_link(4, 3, true);
    net.run(45000);
    int back = 0;
    for (const BleMsg& m : net.msgs(0x02)) back += (m.data[10] == 3 && m.data[11] == 3) ? 1 : 0;   // RIENTRO
    TEST_ASSERT_EQUAL(1, back);
    TEST_ASSERT_EQUAL(0, state_row(net.msgs(0x01).back(), 3)[1]);
}

void test_sos_evento_stato_e_chiusura_dall_app() {
    Net net;
    net.run(6000);
    net.node[1]->start_run(net.now);
    net.run(5000);
    net.node[3]->toggle_sos(net.now);
    net.run(7000);  // un EVENTO subito, uno STATO entro 5 s
    int sos = 0;
    for (const BleMsg& m : net.msgs(0x02)) sos += (m.data[10] == 3 && m.data[11] == 2) ? 1 : 0;
    TEST_ASSERT_EQUAL(1, sos);
    TEST_ASSERT_EQUAL(3, state_row(net.msgs(0x01).back(), 3)[1]);
    // L'app lo chiude: il capogruppo non lo ripropone anche se il 3 continua a segnalarlo (SOS ogni 10 s).
    net.ble.clear();
    ble::Command c = {ble::CommandCode::CloseAlarm, 3, ble::AlarmKind::Sos, 0};
    net.node[1]->on_app_command(c, net.now);
    net.run(35000);
    int again = 0;
    for (const BleMsg& m : net.msgs(0x02)) again += (m.data[10] == 3 && m.data[11] == 2) ? 1 : 0;
    TEST_ASSERT_EQUAL(0, again);
    TEST_ASSERT_EQUAL(0, state_row(net.msgs(0x01).back(), 3)[1]);
}

void test_comandi_dell_app_e_conferme() {
    Net net;
    net.run(6000);
    ble::Command start = {ble::CommandCode::Start, 0, ble::AlarmKind::PreAlarm, 0};
    net.node[1]->on_app_command(start, net.now);
    net.run(100);
    std::vector<BleMsg> conf = net.msgs(0x03);
    TEST_ASSERT_EQUAL(1, conf.size());
    const uint8_t start_ok[] = {0x01, 0x03, 0x10, 0x00};
    TEST_ASSERT_EQUAL(4, conf[0].len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(start_ok, conf[0].data, 4);
    TEST_ASSERT_TRUE(net.st(1).run_started);
    uint32_t t0_seconds = net.st(1).run_s;
    // START ripetuto: confermato, T0 invariato, nessun nuovo file.
    net.run(2000);
    net.node[1]->on_app_command(start, net.now);
    net.run(100);
    TEST_ASSERT_EQUAL(2, net.msgs(0x03).size());
    TEST_ASSERT_EQUAL(0, net.msgs(0x03)[1].data[3]);
    TEST_ASSERT_UINT32_WITHIN(1, t0_seconds + 2, net.st(1).run_s);
    TEST_ASSERT_EQUAL(1, net.count_kind(BleMsg::Kind::NewRun));
    // CHIUDI_ALLARME su un corridore che non c'e': errore.
    ble::Command close = {ble::CommandCode::CloseAlarm, 9, ble::AlarmKind::Lost, 0};
    net.node[1]->on_app_command(close, net.now);
    net.run(100);
    TEST_ASSERT_EQUAL(1, net.msgs(0x03).back().data[3]);
    // Un corridore non capogruppo rifiuta START con esito errore.
    net.ble.clear();
    net.node[2]->on_app_command(start, net.now);
    BleMsg m;
    TEST_ASSERT_TRUE(net.node[2]->pop_ble(m));
    TEST_ASSERT_EQUAL(1, m.data[3]);
    // STOP: confermato, ripetuto confermato.
    ble::Command stop = {ble::CommandCode::Stop, 0, ble::AlarmKind::PreAlarm, 0};
    net.node[1]->on_app_command(stop, net.now);
    net.node[1]->on_app_command(stop, net.now);
    net.run(100);
    std::vector<BleMsg> c2 = net.msgs(0x03);
    TEST_ASSERT_EQUAL(2, c2.size());
    TEST_ASSERT_EQUAL(0, c2[0].data[3]);
    TEST_ASSERT_EQUAL(0, c2[1].data[3]);
    TEST_ASSERT_EQUAL(RunState::Stopped, net.st(2).run);
}

void test_riprendi_da_conferma_con_l_ultimo_seq_e_rimanda() {
    Net net;
    net.run(6000);
    net.node[1]->start_run(net.now);
    net.run(20000);
    uint32_t last = 0;
    for (const BleMsg& m : net.ble) {
        if (m.save) last = rd32(m.data + 2);
    }
    TEST_ASSERT_TRUE(last >= 4);
    net.ble.clear();
    ble::Command resume = {ble::CommandCode::ResumeFrom, 0, ble::AlarmKind::PreAlarm, 2};
    net.node[1]->on_app_command(resume, net.now);
    net.run(100);
    std::vector<BleMsg> conf = net.msgs(0x03);
    TEST_ASSERT_EQUAL(1, conf.size());
    TEST_ASSERT_EQUAL(8, conf[0].len);
    TEST_ASSERT_EQUAL(0x13, conf[0].data[2]);
    TEST_ASSERT_TRUE(rd32(conf[0].data + 4) >= last);            // l'ultimo seq disponibile
    TEST_ASSERT_EQUAL(1, net.count_kind(BleMsg::Kind::Resend));
    for (const BleMsg& m : net.ble) {
        if (m.kind == BleMsg::Kind::Resend) {
            TEST_ASSERT_EQUAL_UINT32(2, m.from_seq);
            TEST_ASSERT_EQUAL_UINT32(rd32(conf[0].data + 4), m.to_seq);
        }
    }
    // Se l'app e' gia' allineata non c'e' nulla da rimandare.
    net.ble.clear();
    resume.last_seq = rd32(conf[0].data + 4) + 100;
    net.node[1]->on_app_command(resume, net.now);
    net.run(100);
    TEST_ASSERT_EQUAL(0, net.count_kind(BleMsg::Kind::Resend));
}

void test_riavvio_del_capogruppo_riprende_il_seq_senza_nuovo_file() {
    Net net;
    net.run(6000);
    net.node[1]->start_run(net.now);
    net.run(15000);
    // Il capogruppo si riavvia: nodo nuovo, con l'ultimo seq letto dal file.
    net.node[1].reset(new Node(1, Role::Leader));
    net.node[1]->seed(77);
    net.node[1]->resume_ble_seq(41);
    net.next_beacon[1] = net.now + 100;
    net.ble.clear();
    net.run(15000);
    TEST_ASSERT_TRUE(net.st(1).run_started);                      // T0 ripreso dai beacon
    TEST_ASSERT_EQUAL(0, net.count_kind(BleMsg::Kind::NewRun));   // stesso file
    bool first = true;
    for (const BleMsg& m : net.ble) {
        if (!m.save) continue;
        if (first) TEST_ASSERT_EQUAL_UINT32(42, rd32(m.data + 2));  // riprende da 41 + 1
        first = false;
    }
    TEST_ASSERT_FALSE(first);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_salti_lungo_la_catena);
    RUN_TEST(test_start_si_propaga_e_si_rilancia_una_volta);
    RUN_TEST(test_stop_si_propaga);
    RUN_TEST(test_telemetria_a_due_salti_arriva_al_capogruppo);
    RUN_TEST(test_partizione_i_record_restano_in_coda_e_poi_arrivano_senza_doppioni);
    RUN_TEST(test_conferme_perse_rimandano_senza_doppioni_a_valle);
    RUN_TEST(test_allarme_sos_rilanciato_una_volta_e_ripetuto);
    RUN_TEST(test_chi_ha_perso_lo_start_prende_t0_dai_beacon);
    RUN_TEST(test_chi_ha_perso_lo_stop_lo_prende_dai_beacon);
    RUN_TEST(test_la_coda_si_ferma_a_600_record);
    RUN_TEST(test_f2_unicast_a_rotazione_con_conferma);
    RUN_TEST(test_f2_tre_tentativi_poi_basta);
    RUN_TEST(test_stato_prima_dello_start_ha_seq_zero_e_non_si_salva);
    RUN_TEST(test_stato_dopo_lo_start_numera_salva_e_riempie_la_tabella);
    RUN_TEST(test_nuovo_file_a_ogni_start_e_seq_che_riparte);
    RUN_TEST(test_perso_confermato_da_due_osservatori_evento_e_rientro);
    RUN_TEST(test_sos_evento_stato_e_chiusura_dall_app);
    RUN_TEST(test_comandi_dell_app_e_conferme);
    RUN_TEST(test_riprendi_da_conferma_con_l_ultimo_seq_e_rimanda);
    RUN_TEST(test_riavvio_del_capogruppo_riprende_il_seq_senza_nuovo_file);
    return UNITY_END();
}
