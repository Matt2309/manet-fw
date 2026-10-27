#include <unity.h>

#include "config.h"
#include "neighbors.h"

void setUp() {}
void tearDown() {}

void test_aggiorna_e_conta() {
    NeighborTable t;
    TEST_ASSERT_NULL(t.find(1));
    TEST_ASSERT_TRUE(t.update(1, -60, 1000));
    TEST_ASSERT_TRUE(t.update(1, -55, 1500));
    const Neighbor* n = t.find(1);
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_EQUAL(-55, n->rssi);
    TEST_ASSERT_EQUAL(1500, n->last_ms);
    TEST_ASSERT_EQUAL(2, n->count);
    TEST_ASSERT_EQUAL(1, t.size());
}

void test_voci_senza_se_stesso_in_ordine_di_id() {
    NeighborTable t;
    t.update(3, -70, 1000);
    t.update(2, -50, 1200);
    t.update(1, -61, 900);
    packets::BeaconEntry e[packets::MAX_NEIGHBORS];
    uint8_t n = t.fill_entries(2, 2000, e);
    TEST_ASSERT_EQUAL(2, n);
    TEST_ASSERT_EQUAL(1, e[0].id);
    TEST_ASSERT_EQUAL(-61, e[0].signal);
    TEST_ASSERT_EQUAL(0, e[0].slope);
    TEST_ASSERT_EQUAL(11, e[0].age_ds);  // 1100 ms
    TEST_ASSERT_EQUAL(3, e[1].id);
    TEST_ASSERT_EQUAL(10, e[1].age_ds);  // 1000 ms
    TEST_ASSERT_EQUAL(255, e[0].sigma);  // il Kalman non c'e' ancora
}

void test_eta_saturata() {
    NeighborTable t;
    t.update(1, -60, 0);
    packets::BeaconEntry e[packets::MAX_NEIGHBORS];
    TEST_ASSERT_EQUAL(1, t.fill_entries(2, 60000, e));
    TEST_ASSERT_EQUAL(255, e[0].age_ds);
}

void test_limite_di_19() {
    NeighborTable t;
    for (uint8_t id = 1; id <= packets::MAX_NEIGHBORS; id++) TEST_ASSERT_TRUE(t.update(id, -60, 0));
    TEST_ASSERT_FALSE(t.update(MAX_NODES + 1, -60, 0));
    TEST_ASSERT_TRUE(t.update(5, -50, 10));  // uno noto si aggiorna ancora
    TEST_ASSERT_EQUAL(packets::MAX_NEIGHBORS, t.size());
}

void test_sentiti_di_recente() {
    NeighborTable t;
    TEST_ASSERT_EQUAL(0, t.heard(5000, 2000));
    t.update(1, -60, 3000);
    t.update(2, -60, 4500);
    t.update(3, -60, 1000);
    TEST_ASSERT_EQUAL(2, t.heard(5000, 2000));  // 1 (2000 ms fa) e 2; il 3 e' troppo vecchio
    TEST_ASSERT_EQUAL(3, t.heard(5000, 4000));
}

void test_tabella_vuota() {
    NeighborTable t;
    packets::BeaconEntry e[packets::MAX_NEIGHBORS];
    TEST_ASSERT_EQUAL(0, t.fill_entries(1, 100, e));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_aggiorna_e_conta);
    RUN_TEST(test_voci_senza_se_stesso_in_ordine_di_id);
    RUN_TEST(test_eta_saturata);
    RUN_TEST(test_limite_di_19);
    RUN_TEST(test_sentiti_di_recente);
    RUN_TEST(test_tabella_vuota);
    return UNITY_END();
}
