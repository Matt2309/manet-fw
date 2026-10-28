// ID della scheda: salvato in NVS con il comando seriale "id N"; in mancanza, la tabella di include/nodes.h
// (le schede gia' in uso); altrimenti 0 (da assegnare).
#pragma once

#include <stdint.h>

namespace identity {

enum class Source : uint8_t { None, Table, Nvs };

struct Id {
    uint8_t id;
    Source source;
};

Id load(const uint8_t mac[6]);

// 1..MAX_NODES salva l'ID; 0 toglie quello salvato. false se l'NVS non risponde.
bool save(int id);

const char* source_name(Source s);

}  // namespace identity
