#include <unity.h>

#include "battery.h"

void setUp() {}
void tearDown() {}

void test_estremi() {
    TEST_ASSERT_EQUAL(100, battery_percent(4200));
    TEST_ASSERT_EQUAL(100, battery_percent(4350));
    TEST_ASSERT_EQUAL(0, battery_percent(3300));
    TEST_ASSERT_EQUAL(0, battery_percent(2900));
}

void test_punti_della_tabella() {
    TEST_ASSERT_EQUAL(50, battery_percent(3840));
    TEST_ASSERT_EQUAL(20, battery_percent(3730));
    TEST_ASSERT_EQUAL(5, battery_percent(3610));
}

void test_interpolazione() {
    // a meta' tra 3840 (50%) e 3850 (55%)
    TEST_ASSERT_EQUAL(53, battery_percent(3845));  // 52,5 arrotondato
    // a meta' tra 3610 (5%) e 3300 (0%)
    TEST_ASSERT_EQUAL(3, battery_percent(3455));
}

void test_monotona() {
    int prev = 0;
    for (int mv = 3200; mv <= 4300; mv += 5) {
        int p = battery_percent(mv);
        TEST_ASSERT_TRUE(p >= prev);
        TEST_ASSERT_TRUE(p >= 0 && p <= 100);
        prev = p;
    }
}

void test_media_su_10() {
    BatteryAverage m;
    TEST_ASSERT_FALSE(m.ready());
    TEST_ASSERT_EQUAL(0, m.average_mv());
    for (int i = 0; i < 10; i++) m.add(4000);
    TEST_ASSERT_EQUAL(4000, m.average_mv());
    // un'altra lettura sposta la media di un decimo della differenza
    m.add(4100);
    TEST_ASSERT_EQUAL(4010, m.average_mv());
}

void test_media_parziale_e_finestra() {
    BatteryAverage m;
    m.add(3900);
    m.add(3800);
    TEST_ASSERT_EQUAL(3850, m.average_mv());
    for (int i = 0; i < 10; i++) m.add(3700);  // le vecchie escono dalla finestra
    TEST_ASSERT_EQUAL(3700, m.average_mv());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_estremi);
    RUN_TEST(test_punti_della_tabella);
    RUN_TEST(test_interpolazione);
    RUN_TEST(test_monotona);
    RUN_TEST(test_media_su_10);
    RUN_TEST(test_media_parziale_e_finestra);
    return UNITY_END();
}
