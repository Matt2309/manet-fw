#include "gnss.h"

#include <Arduino.h>
#include <string.h>

#include "app.h"
#include "logger.h"
#include "nmea.h"
#include "pins.h"

GpsStatus g_gps = {};

namespace {

void gnss_task(void*) {
    Serial1.setRxBufferSize(4096);
    Serial1.begin(pins::gnss_baud, SERIAL_8N1, pins::gnss_rx, pins::gnss_tx);

    NmeaParser parser;
    for (;;) {
        // Lettura ogni 2 ms: il t_ms delle frasi ha una precisione di pochi ms.
        while (Serial1.available() > 0) {
            char c = static_cast<char>(Serial1.read());
            GpsEpoch ep;
            if (parser.feed(c, millis(), ep)) {
                LogEvent e;
                memset(&e, 0, sizeof(e));
                e.type = LogEventType::Gps;
                e.t_ms = ep.t_ms;
                e.u.gps = ep;
                logger::post(e);

                g_gps.fix = ep.fix;
                g_gps.sat = ep.has_sat ? ep.sat : 0;
                g_gps.last_ms = millis();
                g_gps.seen = true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

}  // namespace

void gnss_start() { xTaskCreatePinnedToCore(gnss_task, "gnss", 4096, nullptr, 3, nullptr, 1); }
