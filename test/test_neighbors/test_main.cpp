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

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_aggiorna_e_conta);
    RUN_TEST(test_limite_di_19);
    RUN_TEST(test_sentiti_di_recente);
    return UNITY_END();
}
