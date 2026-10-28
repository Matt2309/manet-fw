#include "identity.h"

#include <Preferences.h>

#include "config.h"
#include "nodes.h"

namespace identity {

Id load(const uint8_t mac[6]) {
    Preferences prefs;
    if (prefs.begin(ROLE_NVS_NAMESPACE, false)) {
        uint8_t id = prefs.getUChar(ID_NVS_KEY, 0);
        prefs.end();
        if (id >= 1 && id <= MAX_NODES) return Id{id, Source::Nvs};
    }
    uint8_t id = node_id_from_mac(mac);
    return Id{id, id != 0 ? Source::Table : Source::None};
}

bool save(int id) {
    Preferences prefs;
    if (!prefs.begin(ROLE_NVS_NAMESPACE, false)) return false;
    if (id == 0) {
        prefs.remove(ID_NVS_KEY);
    } else {
        prefs.putUChar(ID_NVS_KEY, static_cast<uint8_t>(id));
    }
    prefs.end();
    return true;
}

const char* source_name(Source s) {
    switch (s) {
        case Source::Nvs: return "nvs";
        case Source::Table: return "tabella";
        default: return "nessuna";
    }
}

}  // namespace identity
