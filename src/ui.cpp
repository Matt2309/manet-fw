#include "ui.h"

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include <stdio.h>
#include <string.h>

#include "app.h"
#include "battery.h"
#include "ble.h"
#include "button.h"
#include "config.h"
#include "logger.h"
#include "logic.h"
#include "pins.h"
#include "role.h"

// Pannello ST7735S 160x80 della Wireless Tracker V1.1. Offset e inversione dal
// driver Heltec (HT_st7735.h: XSTART 1, YSTART 26, INVON); in verticale
// diventano offset_x = 26, offset_y = 1.
class Display : public lgfx::LGFX_Device {
    lgfx::Panel_ST7735S panel_;
    lgfx::Bus_SPI bus_;

public:
    Display() {
        {
            auto cfg = bus_.config();
            cfg.spi_host = SPI3_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = 27000000;
            cfg.pin_sclk = pins::tft_sclk;
            cfg.pin_mosi = pins::tft_mosi;
            cfg.pin_miso = -1;
            cfg.pin_dc = pins::tft_dc;
            bus_.config(cfg);
            panel_.setBus(&bus_);
        }
        {
            auto cfg = panel_.config();
            cfg.pin_cs = pins::tft_cs;
            cfg.pin_rst = pins::tft_rst;
            cfg.pin_busy = -1;
            cfg.panel_width = 80;
            cfg.panel_height = 160;
            cfg.offset_x = 26;
            cfg.offset_y = 1;
            cfg.invert = true;
            cfg.rgb_order = false;
            panel_.config(cfg);
        }
        setPanel(&panel_);
    }
};

volatile int g_battery_pct = 0;

namespace {

constexpr int W = 160;
constexpr int H = 80;
constexpr int ROW = 20;

Display g_tft;
lgfx::LGFX_Sprite g_spr(&g_tft);

Button g_button(BTN_DEBOUNCE_MS, BTN_SHORT_MAX_MS, BTN_LONG_MS);
BatteryAverage g_bat;
int g_evt_count = 0;
uint32_t g_led_off_at = 0;
bool g_led_on = false;
bool g_backlight = true;

// Lettura della batteria in mV, con il partitore abilitato solo per la misura.
int read_battery_mv() {
    digitalWrite(pins::vbat_enable, HIGH);
    delay(10);
    uint32_t mv = analogReadMilliVolts(pins::vbat_adc);
    digitalWrite(pins::vbat_enable, LOW);
    return static_cast<int>(mv * pins::vbat_divider * BAT_CAL + 0.5f);
}

void led(bool on) {
    digitalWrite(pins::led, on ? HIGH : LOW);
    g_led_on = on;
}

// Scrive una riga; se e' troppo larga riduce il carattere fino a farla stare.
void draw_row(int row, const char* s) {
    float scale = 1.0f;
    g_spr.setTextSize(scale);
    while (g_spr.textWidth(s) > W && scale > 0.5f) {
        scale -= 0.05f;
        g_spr.setTextSize(scale);
    }
    g_spr.setCursor(0, row * ROW + 2);
    g_spr.print(s);
}

// Una pressione che inizia adesso conterebbe come cambio di ruolo (finestra dopo l'accensione).
bool in_role_window(const NodeStatus& st, uint32_t now) {
    return role_can_toggle(now + BTN_LONG_MS, st.run_started);
}

enum class Warning : uint8_t { None, Isolated, MemoryFull, Sos };

// Avviso in grande, per priorita': SOS, memoria piena, isolato.
Warning current_warning(const NodeStatus& ns) {
    if (ns.sos) return Warning::Sos;
    if (logger::state() == logger::State::Full) return Warning::MemoryFull;
    if (ns.isolated) return Warning::Isolated;
    return Warning::None;
}

// Testo grande, centrato, al massimo di scala 2 e comunque dentro lo schermo.
void draw_big(const char* text, uint16_t color) {
    float scale = 2.0f;
    g_spr.setTextSize(scale);
    while (g_spr.textWidth(text) > W && scale > 0.5f) {
        scale -= 0.05f;
        g_spr.setTextSize(scale);
    }
    g_spr.setTextColor(color, TFT_BLACK);
    g_spr.setCursor((W - g_spr.textWidth(text)) / 2, ROW + 10);
    g_spr.print(text);
    g_spr.setTextColor(TFT_WHITE, TFT_BLACK);
}

void draw_display(uint32_t now, bool blink_on, const NodeStatus& ns, Warning warn) {
    char s[48];
    g_spr.fillSprite(TFT_BLACK);
    g_spr.setTextColor(TFT_WHITE, TFT_BLACK);

    // Riga 1: id e ruolo, stato della corsa con il tempo (oppure il MAC se la scheda non e' in tabella).
    if (g_node.id == 0) {
        snprintf(s, sizeof(s), "ID 0 %s", g_node.mac_str);
    } else if (ns.run == RunState::Running) {
        snprintf(s, sizeof(s), "ID %d %s CORSA %02lu:%02lu", g_node.id, role_name(ns.role),
                 static_cast<unsigned long>(ns.run_s / 60), static_cast<unsigned long>(ns.run_s % 60));
    } else if (ns.run == RunState::Stopped) {
        snprintf(s, sizeof(s), "ID %d %s FERMA %02lu:%02lu", g_node.id, role_name(ns.role),
                 static_cast<unsigned long>(ns.run_s / 60), static_cast<unsigned long>(ns.run_s % 60));
    } else {
        snprintf(s, sizeof(s), "ID %d %s PRONTA", g_node.id, role_name(ns.role));
    }
    draw_row(0, s);

    if (warn != Warning::None) {
        // Avviso in grande al posto delle righe centrali.
        if (warn == Warning::Sos) {
            draw_big("SOS INVIATO", TFT_RED);
        } else if (warn == Warning::MemoryFull) {
            draw_big("MEMORIA PIENA", TFT_YELLOW);
        } else {
            draw_big("ISOLATO", TFT_ORANGE);
        }
    } else {
        // Riga 2: GNSS.
        bool recente = g_gps.seen && (now - g_gps.last_ms) < 3000;
        if (recente && g_gps.valid) {
            snprintf(s, sizeof(s), "GPS OK %2d sat", g_gps.sat);
        } else if (recente && g_gps.fix >= 2) {
            snprintf(s, sizeof(s), "GPS %dD %2d sat", g_gps.fix, g_gps.sat);
        } else {
            snprintf(s, sizeof(s), "GPS attesa");
        }
        draw_row(1, s);

        // Riga 3: avviso piccolo, ruolo, allarmi, oppure salti e compagni sentiti.
        logger::State st = logger::state();
        if (st == logger::State::Saved) {
            draw_row(2, "SALVATO");
        } else if (st == logger::State::Error) {
            draw_row(2, "ERRORE FILE");
        } else if (ns.two_leaders) {
            draw_row(2, "DUE CAPOGRUPPO");
        } else if (in_role_window(ns, now)) {
            snprintf(s, sizeof(s), "premi 3s=ruolo (%lus)", static_cast<unsigned long>((ROLE_WINDOW_MS - now + 999) / 1000));
            draw_row(2, s);
        } else if (ns.pre > 0 || ns.lost > 0) {
            snprintf(s, sizeof(s), "PRE %d PERSI %d", ns.pre, ns.lost);
            draw_row(2, s);
        } else if (ns.role == Role::Leader) {
            // Capogruppo: stato del BLE e corridori in tabella.
            BleState b = ble_state();
            snprintf(s, sizeof(s), "BLE %s gruppo %d", b == BleState::Connected ? "collegato" : b == BleState::Advertising ? "attesa" : "spento",
                     ns.group);
            draw_row(2, s);
        } else {
            char salti[8] = "--";
            if (ns.hops != packets::HOPS_ISOLATED) snprintf(salti, sizeof(salti), "%d", ns.hops);
            snprintf(s, sizeof(s), "salti %s vicini %d", salti, ns.heard);
            draw_row(2, s);
        }
    }

    // Riga 4: eventi e batteria; sotto il 20% lampeggia.
    int mv = g_bat.average_mv();
    int pct = battery_percent(mv);
    if (pct >= BAT_LOW_PCT || blink_on) {
        snprintf(s, sizeof(s), "EVT %d BAT %d%% %d,%02dV", g_evt_count, pct, mv / 1000, (mv % 1000) / 10);
        draw_row(3, s);
    }
    g_spr.pushSprite(0, 0);
}

void on_short_press() {
    if (logger::state() != logger::State::Recording) return;
    g_evt_count++;
    LogEvent e;
    memset(&e, 0, sizeof(e));
    e.type = LogEventType::Evt;
    e.t_ms = millis();
    e.u.evt.n = g_evt_count;
    logger::post(e);
    led(true);
    g_led_off_at = millis() + LED_BLINK_MS;
}

// Pressione lunga: nella finestra dopo l'accensione cambia il ruolo; a corsa avviata e' l'SOS (la logica decide).
void on_long_press() {
    NodeStatus st = {};
    logic_status(st);
    logic_command(in_role_window(st, millis()) ? LogicCmd::ToggleRole : LogicCmd::ToggleSos);
}

void backlight(bool on) {
    digitalWrite(pins::tft_backlight, on ? HIGH : LOW);
    g_backlight = on;
}

void ui_task(void*) {
    uint32_t next_display = 0, next_bat = 0, next_bat_log = 0;
    // Schermo acceso all'avvio per la finestra del ruolo, poi solo dopo un tasto o un avviso.
    uint32_t screen_until = ROLE_WINDOW_MS + DISPLAY_ON_MS / 2;
    Warning last_warn = Warning::None;
    bool was_pressed = false, blink = false;
    for (;;) {
        uint32_t now = millis();
        auto wake = [&]() {
            screen_until = now + DISPLAY_ON_MS;
            next_display = now;
        };

        bool pressed = digitalRead(pins::button) == LOW;
        if (pressed && !was_pressed) wake();
        was_pressed = pressed;
        switch (g_button.update(pressed, now)) {
            case ButtonEvent::Short:
                on_short_press();
                wake();
                break;
            case ButtonEvent::Long:
                on_long_press();
                wake();
                break;
            default:
                break;
        }

        NodeStatus ns = {};
        logic_status(ns);
        // LED: lampeggia finche' l'SOS e' attivo; altrimenti solo il lampo della pressione breve.
        if (ns.sos) {
            led((now / SOS_LED_HALF_PERIOD_MS) % 2 == 0);
        } else if (g_led_on && static_cast<int32_t>(now - g_led_off_at) >= 0) {
            led(false);
        }

        if (static_cast<int32_t>(now - next_bat) >= 0) {
            next_bat = now + 1000;
            g_bat.add(read_battery_mv());
            g_battery_pct = battery_percent(g_bat.average_mv());
        }
        if (static_cast<int32_t>(now - next_bat_log) >= 0) {
            next_bat_log = now + BAT_LOG_INTERVAL_MS;
            if (logger::state() == logger::State::Recording) {
                LogEvent e;
                memset(&e, 0, sizeof(e));
                e.type = LogEventType::Bat;
                e.t_ms = now;
                e.u.bat.mv = g_bat.average_mv();
                logger::post(e);
            }
        }

        Warning warn = current_warning(ns);
        if (warn != last_warn && warn != Warning::None) wake();  // un avviso nuovo accende lo schermo
        last_warn = warn;

        bool on = static_cast<int32_t>(screen_until - now) > 0;
        if (on != g_backlight) backlight(on);
        if (on && static_cast<int32_t>(now - next_display) >= 0) {
            next_display = now + DISPLAY_PERIOD_MS;
            blink = !blink;
            draw_display(now, blink, ns, warn);
        }
        vTaskDelay(pdMS_TO_TICKS(BTN_POLL_MS));
    }
}

}  // namespace

void ui_start() {
    pinMode(pins::led, OUTPUT);
    led(false);
    pinMode(pins::button, INPUT);  // pull-up esterna da 10k
    pinMode(pins::vbat_enable, OUTPUT);
    digitalWrite(pins::vbat_enable, LOW);
    analogSetPinAttenuation(pins::vbat_adc, ADC_2_5db);

    // Media gia' piena alla prima lettura, cosi' la prima riga BAT e' attendibile.
    for (int i = 0; i < BatteryAverage::N; i++) {
        g_bat.add(read_battery_mv());
    }
    g_battery_pct = battery_percent(g_bat.average_mv());

    pinMode(pins::tft_backlight, OUTPUT);
    digitalWrite(pins::tft_backlight, HIGH);
    g_tft.init();
    g_tft.setRotation(1);  // orizzontale 160x80
    g_tft.fillScreen(TFT_BLACK);
    g_spr.setColorDepth(16);
    g_spr.createSprite(W, H);
    g_spr.setFont(&fonts::Font2);

    xTaskCreatePinnedToCore(ui_task, "ui", 8192, nullptr, 2, nullptr, 1);
}
