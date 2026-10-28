#include <unity.h>

#include "kalman.h"

void setUp() {}
void tearDown() {}

// Riferimento: filter_link() e predict() di manet-sim con R = 10,84, sigma_a = 0,25, pendenza iniziale 3,
// riavvio a 10 s. I beacon hanno jitter, un buco di 3 s (6520) e uno di 12 s (20100: il filtro riparte).
struct Riga {
    uint32_t t_ms;
    int z;
    double r, s, p00, p01, p11;
};

static const Riga RIF[] = {
    {1000, -60, -60.0, 0.0, 10.84, 0.0, 9.0},
    {1480, -61, -60.54369238063936, -0.18215261351451834, 5.893625406130619, 1.974534330497379, 8.241789210799976},
    {2010, -60, -60.328218358047465, 0.010159749435090015, 5.282827026837783, 3.256103869832799, 6.367071416929351},
    {2495, -62, -61.125391080971056, -0.5023001036658077, 5.1856139369895375, 3.3130760368225913, 4.456152330785392},
    {3020, -63, -62.15786458656391, -0.9421037145538979, 5.173142871296109, 2.959505309747933, 2.9433688364799084},
    {3490, -63, -62.77740412742334, -1.0314252734609957, 4.797782912630911, 2.424574378902932, 1.9998292864348035},
    {6520, -70, -69.09853589259761, -1.760826181711587, 8.455091258846108, 1.9296992388360688, 0.6278283360230297},
    {7010, -72, -70.96440022457664, -1.9752868815266493, 5.33348810528089, 1.1403347340420684, 0.4223032412761395},
    {7480, -71, -71.55809194080871, -1.9060031453621835, 4.063770292639306, 0.8412278997512257, 0.34724488971027445},
    {8030, -74, -73.05213674591187, -1.9970877074868296, 3.467159149095354, 0.708490362531674, 0.3135377753732109},
    {20100, -80, -80.0, 0.0, 10.84, 0.0, 9.0},
    {20590, -79, -79.45463408186745, 0.18527190750747063, 5.91176655255679, 2.0083474773809815, 8.212185769611041},
    {21070, -82, -80.61205720874479, -0.5775072189285189, 5.128683815333436, 3.1387985135899004, 6.5171795205498455},
};

constexpr double TOL = 1e-9;

void test_coincide_con_il_simulatore() {
    LinkFilter f;
    TEST_ASSERT_FALSE(f.started());
    for (const Riga& k : RIF) {
        f.update(k.t_ms, k.z);
        const LinkEstimate& x = f.posterior();
        TEST_ASSERT_DOUBLE_WITHIN(TOL, k.r, x.r);
        TEST_ASSERT_DOUBLE_WITHIN(TOL, k.s, x.s);
        TEST_ASSERT_DOUBLE_WITHIN(TOL, k.p00, x.p00);
        TEST_ASSERT_DOUBLE_WITHIN(TOL, k.p01, x.p01);
        TEST_ASSERT_DOUBLE_WITHIN(TOL, k.p11, x.p11);
        TEST_ASSERT_EQUAL_UINT32(k.t_ms, f.last_ms());
    }
}

void test_predizione_senza_aggiornare() {
    LinkFilter f;
    for (const Riga& k : RIF) f.update(k.t_ms, k.z);
    LinkEstimate p = f.predicted(21070 + 1234);
    TEST_ASSERT_DOUBLE_WITHIN(TOL, -81.32470111690257, p.r);
    TEST_ASSERT_DOUBLE_WITHIN(TOL, -0.5775072189285189, p.s);
    TEST_ASSERT_DOUBLE_WITHIN(TOL, 22.83846028370104, p.p00);
    TEST_ASSERT_DOUBLE_WITHIN(TOL, 11.22858416694841, p.p01);
    TEST_ASSERT_DOUBLE_WITHIN(TOL, 6.594304520549845, p.p11);
    // La posteriori non cambia: la predizione e' una lettura.
    TEST_ASSERT_DOUBLE_WITHIN(TOL, RIF[12].r, f.posterior().r);
    // A tempo zero la predizione e' la posteriori.
    TEST_ASSERT_DOUBLE_WITHIN(TOL, RIF[12].p00, f.predicted(21070).p00);
}

void test_riavvio_dopo_silenzio_lungo() {
    LinkFilter f;
    f.update(1000, -60);
    f.update(1500, -62);
    f.update(1500 + 10000, -80);  // esattamente 10 s: non riparte ("oltre" il limite)
    TEST_ASSERT_TRUE(f.posterior().r > -80.0 + 1e-6 || f.posterior().r < -80.0 - 1e-6);
    f.update(1500 + 10000 + 10001, -90);  // 10,001 s: riparte da r = z
    TEST_ASSERT_DOUBLE_WITHIN(TOL, -90.0, f.posterior().r);
    TEST_ASSERT_DOUBLE_WITHIN(TOL, 0.0, f.posterior().s);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_coincide_con_il_simulatore);
    RUN_TEST(test_predizione_senza_aggiornare);
    RUN_TEST(test_riavvio_dopo_silenzio_lungo);
    return UNITY_END();
}
