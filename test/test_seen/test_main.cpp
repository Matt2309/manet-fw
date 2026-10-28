#include <unity.h>

#include "seen.h"

void setUp() {}
void tearDown() {}

void test_record_doppioni() {
    SeenRecords s;
    TEST_ASSERT_TRUE(s.add(3, 100));
    TEST_ASSERT_FALSE(s.add(3, 100));
    TEST_ASSERT_TRUE(s.add(3, 101));
    TEST_ASSERT_TRUE(s.add(4, 100));   // altro mittente
    TEST_ASSERT_FALSE(s.add(4, 100));
}

void test_record_fuori_ordine_e_lacune() {
    SeenRecords s;
    TEST_ASSERT_TRUE(s.add(2, 50));
    TEST_ASSERT_TRUE(s.add(2, 60));   // salta in avanti
    TEST_ASSERT_TRUE(s.add(2, 55));   // arriva in ritardo, mai visto
    TEST_ASSERT_FALSE(s.add(2, 55));
    TEST_ASSERT_FALSE(s.add(2, 60));
    TEST_ASSERT_FALSE(s.add(2, 50));
}

void test_record_fuori_finestra() {
    SeenRecords s;
    TEST_ASSERT_TRUE(s.add(2, 2000));
    TEST_ASSERT_FALSE(s.add(2, 2000 - SEEN_RECORD_WINDOW));  // troppo vecchio per essere ricordato
    TEST_ASSERT_TRUE(s.add(2, 2000 - SEEN_RECORD_WINDOW + 1));
    // Un grande salto in avanti azzera la finestra.
    TEST_ASSERT_TRUE(s.add(2, 2000 + 5000));
    TEST_ASSERT_TRUE(s.add(2, 2000 + 5000 - 10));
}

void test_record_un_id_non_valido() {
    SeenRecords s;
    TEST_ASSERT_FALSE(s.add(MAX_NODES + 1, 1));
}

void test_seq_doppioni_e_giro() {
    SeenSeq s;
    TEST_ASSERT_TRUE(s.add(1, 250));
    TEST_ASSERT_FALSE(s.add(1, 250));
    TEST_ASSERT_TRUE(s.add(1, 251));
    TEST_ASSERT_TRUE(s.add(1, 255));
    TEST_ASSERT_TRUE(s.add(1, 0));    // il numero ricomincia
    TEST_ASSERT_TRUE(s.add(1, 1));
    TEST_ASSERT_FALSE(s.add(1, 255)); // gia' visto, prima del giro
    TEST_ASSERT_FALSE(s.add(1, 251));
    TEST_ASSERT_TRUE(s.add(1, 253));  // in ritardo ma mai visto
    TEST_ASSERT_FALSE(s.add(1, 253));
}

void test_seq_dopo_un_giro_completo_si_riconosce_di_nuovo() {
    SeenSeq s;
    for (int i = 0; i < 300; i++) TEST_ASSERT_TRUE(s.add(2, static_cast<uint8_t>(i)));
    // Il 44 e' tornato nuovo dopo 256 passi: l'ultimo visto e' il 43 (299 % 256).
    TEST_ASSERT_FALSE(s.add(2, 43));
    TEST_ASSERT_TRUE(s.add(2, 44));
    TEST_ASSERT_FALSE(s.add(2, 44));
}

void test_seq_creatori_indipendenti() {
    SeenSeq s;
    TEST_ASSERT_TRUE(s.add(1, 7));
    TEST_ASSERT_TRUE(s.add(2, 7));
    TEST_ASSERT_FALSE(s.add(2, 7));
    s.clear();
    TEST_ASSERT_TRUE(s.add(1, 7));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_record_doppioni);
    RUN_TEST(test_record_fuori_ordine_e_lacune);
    RUN_TEST(test_record_fuori_finestra);
    RUN_TEST(test_record_un_id_non_valido);
    RUN_TEST(test_seq_doppioni_e_giro);
    RUN_TEST(test_seq_dopo_un_giro_completo_si_riconosce_di_nuovo);
    RUN_TEST(test_seq_creatori_indipendenti);
    return UNITY_END();
}
