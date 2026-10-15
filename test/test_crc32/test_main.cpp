#include <string.h>
#include <unity.h>

#include "crc32.h"

void setUp() {}
void tearDown() {}

void test_valore_noto() {
    const char* s = "123456789";
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc32_update(0, reinterpret_cast<const uint8_t*>(s), strlen(s)));
}

void test_vuoto() { TEST_ASSERT_EQUAL_HEX32(0u, crc32_update(0, nullptr, 0)); }

void test_a_blocchi() {
    const char* s = "123456789";
    const uint8_t* p = reinterpret_cast<const uint8_t*>(s);
    uint32_t c = crc32_update(0, p, 4);
    c = crc32_update(c, p + 4, 5);
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, c);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_valore_noto);
    RUN_TEST(test_vuoto);
    RUN_TEST(test_a_blocchi);
    return UNITY_END();
}
