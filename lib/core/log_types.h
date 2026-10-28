// Tipi condivisi fra la logica e le righe del registro.
#pragma once

#include <stdint.h>

enum class TelAction : uint8_t { Produced, Sent, Confirmed, Received, Queued, Failed };
enum class CmdOrigin : uint8_t { App, Radio };
enum class BleEvent : uint8_t { Connected, Dropped };
enum class AlmAction : uint8_t { Created, Received, Relayed };
