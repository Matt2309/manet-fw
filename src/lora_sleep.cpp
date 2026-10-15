#include "lora_sleep.h"

#include <Arduino.h>
#include <SPI.h>

#include "pins.h"

namespace {

bool wait_busy_low(uint32_t timeout_ms) {
    uint32_t t0 = millis();
    while (digitalRead(pins::lora_busy) == HIGH) {
        if (millis() - t0 > timeout_ms) return false;
        delay(1);
    }
    return true;
}

}  // namespace

bool lora_sleep(char* msg, size_t cap) {
    pinMode(pins::lora_nss, OUTPUT);
    digitalWrite(pins::lora_nss, HIGH);
    pinMode(pins::lora_rst, OUTPUT);
    pinMode(pins::lora_busy, INPUT);

    // Reset hardware.
    digitalWrite(pins::lora_rst, LOW);
    delay(2);
    digitalWrite(pins::lora_rst, HIGH);
    delay(10);
    if (!wait_busy_low(200)) {
        snprintf(msg, cap, "lora errore: BUSY alto dopo il reset");
        return false;
    }

    SPI.begin(pins::lora_sck, pins::lora_miso, pins::lora_mosi, pins::lora_nss);
    SPISettings settings(1000000, MSBFIRST, SPI_MODE0);

    // GetStatus (0xC0): il byte letto ha il modo del chip nei bit 6..4.
    SPI.beginTransaction(settings);
    digitalWrite(pins::lora_nss, LOW);
    SPI.transfer(0xC0);
    uint8_t status = SPI.transfer(0x00);
    digitalWrite(pins::lora_nss, HIGH);
    SPI.endTransaction();
    uint8_t mode = (status >> 4) & 0x07;  // 2 = STBY_RC, 3 = STBY_XOSC
    bool risponde = (mode == 2 || mode == 3);

    if (risponde) {
        // SetSleep (0x84), configurazione 0x00: avvio a freddo.
        wait_busy_low(50);
        SPI.beginTransaction(settings);
        digitalWrite(pins::lora_nss, LOW);
        SPI.transfer(0x84);
        SPI.transfer(0x00);
        digitalWrite(pins::lora_nss, HIGH);
        SPI.endTransaction();
        delay(2);
    }
    SPI.end();
    // NSS resta alto, altrimenti il chip si risveglia.
    pinMode(pins::lora_nss, OUTPUT);
    digitalWrite(pins::lora_nss, HIGH);

    if (risponde) {
        snprintf(msg, cap, "lora sleep ok (stato 0x%02X)", status);
    } else {
        snprintf(msg, cap, "lora errore: stato 0x%02X inatteso", status);
    }
    return risponde;
}
