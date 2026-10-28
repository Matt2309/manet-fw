#include <unity.h>

#include "geo.h"

void setUp() {}
void tearDown() {}

static packets::Position at(double lat, double lon) {
    return packets::Position{packets::degrees_to_e7(lat), packets::degrees_to_e7(lon), 10};
}

void test_stesso_punto() { TEST_ASSERT_EQUAL_DOUBLE(0.0, distance_m(at(45.0, 9.0), at(45.0, 9.0))); }

void test_un_millesimo_di_grado_di_latitudine() {
    // 0,001 gradi di latitudine = 111,19 m con raggio 6371 km.
    TEST_ASSERT_DOUBLE_WITHIN(0.05, 111.19, distance_m(at(45.0, 9.0), at(45.001, 9.0)));
}

void test_la_longitudine_si_accorcia_con_la_latitudine() {
    // 0,001 gradi di longitudine a 45 N = 111,19 * cos(45) = 78,62 m; all'equatore 111,19 m.
    TEST_ASSERT_DOUBLE_WITHIN(0.05, 78.62, distance_m(at(45.0, 9.0), at(45.0, 9.001)));
    TEST_ASSERT_DOUBLE_WITHIN(0.05, 111.19, distance_m(at(0.0, 9.0), at(0.0, 9.001)));
}

void test_diagonale_e_simmetria() {
    packets::Position a = at(44.4936, 11.3426), b = at(44.4940, 11.3432);
    double d = distance_m(a, b);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, d, distance_m(b, a));
    TEST_ASSERT_DOUBLE_WITHIN(0.1, 65.14, d);  // 44,48 m a nord e 47,6 m a est
}

void test_emisferi_e_precisione_del_metro() {
    // Un metro e' circa 90 unita' di 1e-7 gradi: la risoluzione del formato basta.
    packets::Position a = {-338000000, 1511000000, 10}, b = {-338000000 + 90, 1511000000, 10};
    TEST_ASSERT_DOUBLE_WITHIN(0.02, 1.0, distance_m(a, b));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_stesso_punto);
    RUN_TEST(test_un_millesimo_di_grado_di_latitudine);
    RUN_TEST(test_la_longitudine_si_accorcia_con_la_latitudine);
    RUN_TEST(test_diagonale_e_simmetria);
    RUN_TEST(test_emisferi_e_precisione_del_metro);
    return UNITY_END();
}
