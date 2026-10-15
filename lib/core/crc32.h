// CRC32 IEEE 802.3, identico a zlib.crc32() di Python.
#pragma once

#include <stddef.h>
#include <stdint.h>

// Aggiorna il CRC con altri byte; si parte da crc = 0.
uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len);
