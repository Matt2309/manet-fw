// Rete di Node in memoria: radio simulata (broadcast a chi e' in portata, unicast con esito), nessun hardware.
// Catena di default 1 - 2 - 3: il capogruppo (1) e il 3 non si sentono, tutto passa dal 2.
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

constexpr int N = 3;
constexpr uint32_t STEP_MS = 50;

struct Net {
    std::unique_ptr<Node> node[N + 1];
    bool link[N + 1][N + 1] = {};  // link[a][b]: b sente a
    uint32_t now = 1000;
    uint32_t next_beacon[N + 1] = {};
    int cmd_packets = 0, alarm_packets = 0;
    std::vector<NodeLog> logs[N + 1];
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
};

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
    for (int i = 1; i <= N; i++) TEST_ASSERT_TRUE(net.st(i).run_started);
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
    for (int i = 1; i <= N; i++) TEST_ASSERT_EQUAL(RunState::Stopped, net.st(i).run);
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
    return UNITY_END();
}
