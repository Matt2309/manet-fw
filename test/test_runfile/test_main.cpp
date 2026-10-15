#include <unity.h>

#include "runfile.h"

void setUp() {}
void tearDown() {}

void test_numero_dal_nome() {
    TEST_ASSERT_EQUAL(4, run_number("run_004.csv"));
    TEST_ASSERT_EQUAL(4, run_number("/run_004.csv"));
    TEST_ASSERT_EQUAL(999, run_number("run_999.csv"));
    TEST_ASSERT_EQUAL(1, run_number("run_001.csv"));
}

void test_nomi_non_validi() {
    TEST_ASSERT_EQUAL(-1, run_number("run_000.csv"));
    TEST_ASSERT_EQUAL(-1, run_number("run_4.csv"));
    TEST_ASSERT_EQUAL(-1, run_number("run_1000.csv"));
    TEST_ASSERT_EQUAL(-1, run_number("run_00a.csv"));
    TEST_ASSERT_EQUAL(-1, run_number("run_004.txt"));
    TEST_ASSERT_EQUAL(-1, run_number("log_004.csv"));
    TEST_ASSERT_EQUAL(-1, run_number("run_004.csv.bak"));
    TEST_ASSERT_EQUAL(-1, run_number(""));
    TEST_ASSERT_EQUAL(-1, run_number(nullptr));
}

void test_formato_nome() {
    char b[16];
    TEST_ASSERT_TRUE(run_name(4, b, sizeof(b)));
    TEST_ASSERT_EQUAL_STRING("/run_004.csv", b);
    TEST_ASSERT_TRUE(run_name(999, b, sizeof(b)));
    TEST_ASSERT_EQUAL_STRING("/run_999.csv", b);
    TEST_ASSERT_FALSE(run_name(0, b, sizeof(b)));
    TEST_ASSERT_FALSE(run_name(1000, b, sizeof(b)));
    char piccolo[8];
    TEST_ASSERT_FALSE(run_name(4, piccolo, sizeof(piccolo)));
}

void test_primo_libero() {
    TEST_ASSERT_EQUAL(1, run_first_free(nullptr, 0));
    int a[] = {1, 2, 3};
    TEST_ASSERT_EQUAL(4, run_first_free(a, 3));
    int b[] = {1, 3, 4};  // buco in 2
    TEST_ASSERT_EQUAL(2, run_first_free(b, 3));
    int c[] = {4, 2, 1};  // fuori ordine
    TEST_ASSERT_EQUAL(3, run_first_free(c, 3));
}

void test_tutti_occupati() {
    static int tutti[RUN_MAX];
    for (int i = 0; i < RUN_MAX; i++) tutti[i] = i + 1;
    TEST_ASSERT_EQUAL(-1, run_first_free(tutti, RUN_MAX));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_numero_dal_nome);
    RUN_TEST(test_nomi_non_validi);
    RUN_TEST(test_formato_nome);
    RUN_TEST(test_primo_libero);
    RUN_TEST(test_tutti_occupati);
    return UNITY_END();
}
