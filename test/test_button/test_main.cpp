#include <unity.h>

#include "button.h"

void setUp() {}
void tearDown() {}

static Button nuovo() { return Button(30, 1000, 3000); }

// Tiene il livello 'premuto' da t0 a t1 (ms) campionando ogni 10 ms; restituisce
// il primo evento diverso da None e quanti eventi ci sono stati.
struct Esito {
    ButtonEvent primo = ButtonEvent::None;
    int eventi = 0;
};

static void campiona(Button& b, bool livello, uint32_t t0, uint32_t t1, Esito& e) {
    for (uint32_t t = t0; t < t1; t += 10) {
        ButtonEvent ev = b.update(livello, t);
        if (ev != ButtonEvent::None) {
            if (e.eventi == 0) e.primo = ev;
            e.eventi++;
        }
    }
}

void test_pressione_breve() {
    Button b = nuovo();
    Esito e;
    campiona(b, false, 0, 500, e);
    campiona(b, true, 500, 700, e);
    campiona(b, false, 700, 1000, e);
    TEST_ASSERT_EQUAL(1, e.eventi);
    TEST_ASSERT_TRUE(e.primo == ButtonEvent::Short);
}

void test_rimbalzi_filtrati() {
    Button b = nuovo();
    Esito e;
    campiona(b, false, 0, 500, e);
    // rimbalzi da 10 ms: mai stabili per 30 ms
    for (uint32_t t = 500; t < 600; t += 20) {
        b.update(true, t);
        b.update(false, t + 10);
    }
    campiona(b, false, 600, 1000, e);
    TEST_ASSERT_EQUAL(0, e.eventi);
}

void test_rimbalzo_in_pressione_e_rilascio() {
    Button b = nuovo();
    Esito e;
    campiona(b, false, 0, 500, e);
    b.update(true, 500);
    b.update(false, 510);  // rimbalzo
    campiona(b, true, 520, 700, e);
    b.update(false, 700);
    b.update(true, 710);  // rimbalzo in rilascio
    campiona(b, false, 720, 1000, e);
    TEST_ASSERT_EQUAL(1, e.eventi);
    TEST_ASSERT_TRUE(e.primo == ButtonEvent::Short);
}

void test_pressione_media_ignorata() {
    Button b = nuovo();
    Esito e;
    campiona(b, false, 0, 500, e);
    campiona(b, true, 500, 2500, e);  // 2 s
    campiona(b, false, 2500, 3000, e);
    TEST_ASSERT_EQUAL(0, e.eventi);
}

void test_pressione_lunga_a_3_secondi() {
    Button b = nuovo();
    Esito e;
    campiona(b, false, 0, 500, e);
    // premuto da 500: l'evento arriva a 500 + 3000
    for (uint32_t t = 500; t < 3490; t += 10) {
        TEST_ASSERT_TRUE(b.update(true, t) == ButtonEvent::None);
    }
    TEST_ASSERT_TRUE(b.update(true, 3500) == ButtonEvent::Long);
}

void test_lunga_senza_doppio_scatto_ne_breve() {
    Button b = nuovo();
    Esito e;
    campiona(b, false, 0, 500, e);
    campiona(b, true, 500, 6000, e);  // tenuto a lungo
    campiona(b, false, 6000, 6500, e);
    TEST_ASSERT_EQUAL(1, e.eventi);
    TEST_ASSERT_TRUE(e.primo == ButtonEvent::Long);
}

void test_premuto_all_avvio_ignorato() {
    Button b = nuovo();
    Esito e;
    campiona(b, true, 0, 5000, e);  // tenuto premuto dall'accensione, anche oltre 3 s
    campiona(b, false, 5000, 5500, e);
    TEST_ASSERT_EQUAL(0, e.eventi);
    // dopo il rilascio funziona normalmente
    campiona(b, true, 5500, 5700, e);
    campiona(b, false, 5700, 6000, e);
    TEST_ASSERT_EQUAL(1, e.eventi);
    TEST_ASSERT_TRUE(e.primo == ButtonEvent::Short);
}

void test_due_pressioni_brevi() {
    Button b = nuovo();
    Esito e;
    campiona(b, false, 0, 100, e);
    campiona(b, true, 100, 250, e);
    campiona(b, false, 250, 500, e);
    campiona(b, true, 500, 650, e);
    campiona(b, false, 650, 900, e);
    TEST_ASSERT_EQUAL(2, e.eventi);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_pressione_breve);
    RUN_TEST(test_rimbalzi_filtrati);
    RUN_TEST(test_rimbalzo_in_pressione_e_rilascio);
    RUN_TEST(test_pressione_media_ignorata);
    RUN_TEST(test_pressione_lunga_a_3_secondi);
    RUN_TEST(test_lunga_senza_doppio_scatto_ne_breve);
    RUN_TEST(test_premuto_all_avvio_ignorato);
    RUN_TEST(test_due_pressioni_brevi);
    return UNITY_END();
}
