#include "ninjamplugin/format/clap/plugin_entry.h"

extern "C" {

CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION,
    NinjamPluginEntryInit,
    NinjamPluginEntryDeinit,
    NinjamPluginEntryGetFactory,
};

}  // extern "C"
