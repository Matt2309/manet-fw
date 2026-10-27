#include <unity.h>

#include "config.h"
#include "role.h"

void setUp() {}
void tearDown() {}

void test_finestra_del_ruolo() {
    TEST_ASSERT_TRUE(role_can_toggle(0, false));
    TEST_ASSERT_TRUE(role_can_toggle(ROLE_WINDOW_MS - 1, false));
    TEST_ASSERT_FALSE(role_can_toggle(ROLE_WINDOW_MS, false));
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
