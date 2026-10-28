#include <string.h>
#include <unity.h>

#include "config.h"
#include "node.h"

void setUp() {}
void tearDown() {}

static size_t beacon_da(uint8_t sender, uint8_t hops, uint8_t* buf) {
    packets::Beacon b = {};
    b.sender = sender;
    b.hops = hops;
    b.t_s = packets::T_NOT_STARTED;
    return packets::pack_beacon(b, buf, packets::MAX_SIZE);
}

void test_riceve_un_beacon_e_lo_mette_nei_vicini() {
    Node n(1, Role::Runner);
    uint8_t buf[packets::MAX_SIZE];
    size_t len = beacon_da(2, 1, buf);
    packets::Beacon b;
    TEST_ASSERT_TRUE(n.on_packet(buf, len, -60, 1000, b));
    TEST_ASSERT_EQUAL(2, b.sender);
    TEST_ASSERT_EQUAL(1, n.status(1500).heard);
    TEST_ASSERT_EQUAL(0, n.status(5000).heard);  // piu' vecchio di HEARD_WINDOW_MS
}

void test_scarta_propri_sbagliati_e_non_beacon() {
    Node n(1, Role::Runner);
    uint8_t buf[packets::MAX_SIZE];
    packets::Beacon b;

    size_t len = beacon_da(1, 1, buf);  // il mio stesso id
    TEST_ASSERT_FALSE(n.on_packet(buf, len, -60, 1000, b));
    len = beacon_da(0, 1, buf);  // scheda non in tabella
    TEST_ASSERT_FALSE(n.on_packet(buf, len, -60, 1000, b));
    len = beacon_da(MAX_NODES + 1, 1, buf);
    TEST_ASSERT_FALSE(n.on_packet(buf, len, -60, 1000, b));

    len = beacon_da(2, 1, buf);
    buf[0] = 0x00;  // magic sbagliato
    TEST_ASSERT_FALSE(n.on_packet(buf, len, -60, 1000, b));
    len = beacon_da(2, 1, buf);
    buf[1] = packets::MAGIC;  // versione sbagliata
    TEST_ASSERT_FALSE(n.on_packet(buf, len, -60, 1000, b));
    len = beacon_da(2, 1, buf);
    TEST_ASSERT_FALSE(n.on_packet(buf, len - 1, -60, 1000, b));  // troncato

    packets::Command c = {2, 1, packets::Cmd::Start, 0};
    len = packets::pack_command(c, buf, sizeof(buf));
    TEST_ASSERT_FALSE(n.on_packet(buf, len, -60, 1000, b));  // i comandi arrivano dalla fase 4

    TEST_ASSERT_EQUAL(0, n.status(1000).heard);
}

void test_beacon_spedito() {
    Node n(2, Role::Runner);
    uint8_t rx[packets::MAX_SIZE], tx[packets::MAX_SIZE];
    packets::Beacon b, sent, back;
    n.on_packet(rx, beacon_da(1, 0, rx), -55, 900, b);
    n.on_packet(rx, beacon_da(3, 1, rx), -70, 1000, b);

    size_t len = n.build_beacon(1500, 100, tx, sizeof(tx), sent);
    TEST_ASSERT_EQUAL(11 + 5 * 2, len);
    TEST_ASSERT_TRUE(packets::unpack_beacon(tx, len, back));
    TEST_ASSERT_EQUAL(2, back.sender);
    TEST_ASSERT_EQUAL(0, back.seq);
    TEST_ASSERT_EQUAL(packets::HOPS_ISOLATED, back.hops);
    TEST_ASSERT_EQUAL_UINT16(packets::T_NOT_STARTED, back.t_s);
    TEST_ASSERT_EQUAL(packets::BATTERY_MAX, back.battery);
    TEST_ASSERT_EQUAL(2, back.n);
    TEST_ASSERT_EQUAL(1, back.nb[0].id);
    TEST_ASSERT_EQUAL(-55, back.nb[0].signal);
    TEST_ASSERT_EQUAL(6, back.nb[0].age_ds);  // 600 ms
    TEST_ASSERT_EQUAL(3, back.nb[1].id);

    n.build_beacon(2000, 100, tx, sizeof(tx), sent);
    TEST_ASSERT_EQUAL(1, sent.seq);  // il numero progressivo avanza
}

void test_il_capogruppo_dichiara_zero_salti() {
    Node n(1, Role::Leader);
    uint8_t tx[packets::MAX_SIZE];
    packets::Beacon sent;
    n.build_beacon(100, 50, tx, sizeof(tx), sent);
    TEST_ASSERT_EQUAL(0, sent.hops);
    TEST_ASSERT_EQUAL(0, n.status(100).hops);
}

void test_due_capogruppo_solo_per_il_capogruppo() {
    uint8_t buf[packets::MAX_SIZE];
    packets::Beacon b;

    Node capo(1, Role::Leader);
    TEST_ASSERT_FALSE(capo.status(500).two_leaders);
    capo.on_packet(buf, beacon_da(2, 1, buf), -60, 500, b);  // un corridore non e' un conflitto
    TEST_ASSERT_FALSE(capo.status(600).two_leaders);
    capo.on_packet(buf, beacon_da(3, 0, buf), -60, 1000, b);
    TEST_ASSERT_TRUE(capo.status(1500).two_leaders);
    TEST_ASSERT_FALSE(capo.status(1000 + LEADER_CONFLICT_MS + 1).two_leaders);  // l'altro tace

    Node corridore(2, Role::Runner);
    corridore.on_packet(buf, beacon_da(3, 0, buf), -60, 1000, b);
    TEST_ASSERT_FALSE(corridore.status(1500).two_leaders);
}

void test_cambio_ruolo_nella_finestra() {
    Node n(1, Role::Runner);
    TEST_ASSERT_TRUE(n.toggle_role(2000));
    TEST_ASSERT_EQUAL(Role::Leader, n.role());
    TEST_ASSERT_TRUE(n.toggle_role(BTN_LONG_MS + ROLE_WINDOW_MS - 1));  // pressione iniziata in tempo
    TEST_ASSERT_EQUAL(Role::Runner, n.role());
    TEST_ASSERT_FALSE(n.toggle_role(BTN_LONG_MS + ROLE_WINDOW_MS));
    TEST_ASSERT_EQUAL(Role::Runner, n.role());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_riceve_un_beacon_e_lo_mette_nei_vicini);
    RUN_TEST(test_scarta_propri_sbagliati_e_non_beacon);
    RUN_TEST(test_beacon_spedito);
    RUN_TEST(test_il_capogruppo_dichiara_zero_salti);
    RUN_TEST(test_due_capogruppo_solo_per_il_capogruppo);
    RUN_TEST(test_cambio_ruolo_nella_finestra);
    return UNITY_END();
}
