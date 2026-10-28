#include <unity.h>

#include "config.h"
#include "node_id.h"

void setUp() {}
void tearDown() {}

void test_id_validi() {
    TEST_ASSERT_EQUAL(1, parse_node_id("1"));
    TEST_ASSERT_EQUAL(5, parse_node_id("5"));
    TEST_ASSERT_EQUAL(MAX_NODES, parse_node_id("20"));
    TEST_ASSERT_EQUAL(7, parse_node_id("007"));
}

void test_zero_toglie_l_id_salvato() { TEST_ASSERT_EQUAL(0, parse_node_id("0")); }

void test_testo_non_valido() {
    TEST_ASSERT_EQUAL(-1, parse_node_id(""));
    TEST_ASSERT_EQUAL(-1, parse_node_id(nullptr));
    TEST_ASSERT_EQUAL(-1, parse_node_id("21"));
    TEST_ASSERT_EQUAL(-1, parse_node_id("-3"));
    TEST_ASSERT_EQUAL(-1, parse_node_id("+3"));
    TEST_ASSERT_EQUAL(-1, parse_node_id("5a"));
    TEST_ASSERT_EQUAL(-1, parse_node_id("a"));
    TEST_ASSERT_EQUAL(-1, parse_node_id("99999999999"));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_id_validi);
    RUN_TEST(test_zero_toglie_l_id_salvato);
    RUN_TEST(test_testo_non_valido);
    return UNITY_END();
}
