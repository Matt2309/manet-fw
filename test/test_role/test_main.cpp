#include <unity.h>

#include "config.h"
#include "role.h"

void setUp() {}
void tearDown() {}

void test_finestra_del_ruolo() {
    // La finestra conta dall'inizio della pressione, non dallo scatto.
    TEST_ASSERT_TRUE(role_can_toggle(BTN_LONG_MS + ROLE_WINDOW_MS - 1, false));
    TEST_ASSERT_FALSE(role_can_toggle(BTN_LONG_MS + ROLE_WINDOW_MS, false));
    // Pressione iniziata a 9,8 s, scattata a 12,8 s (il caso della prova al banco).
    TEST_ASSERT_TRUE(role_can_toggle(12800, false));
    // Scatto prima di BTN_LONG_MS: nessun underflow.
    TEST_ASSERT_TRUE(role_can_toggle(0, false));
    TEST_ASSERT_TRUE(role_can_toggle(BTN_LONG_MS - 1, false));
}

void test_no_cambio_a_corsa_avviata() {
    TEST_ASSERT_FALSE(role_can_toggle(1000, true));
}

void test_alterna_e_nvs() {
    TEST_ASSERT_EQUAL(Role::Leader, role_toggled(Role::Runner));
    TEST_ASSERT_EQUAL(Role::Runner, role_toggled(Role::Leader));
    TEST_ASSERT_EQUAL(Role::Leader, role_from_byte(1));
    TEST_ASSERT_EQUAL(Role::Runner, role_from_byte(0));
    TEST_ASSERT_EQUAL(Role::Runner, role_from_byte(0xFF));  // NVS vuota o corrotta
}

void test_nomi() {
    TEST_ASSERT_EQUAL_STRING("CAPO", role_name(Role::Leader));
    TEST_ASSERT_EQUAL_STRING("CORR", role_name(Role::Runner));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_finestra_del_ruolo);
    RUN_TEST(test_no_cambio_a_corsa_avviata);
    RUN_TEST(test_alterna_e_nvs);
    RUN_TEST(test_nomi);
    return UNITY_END();
}
