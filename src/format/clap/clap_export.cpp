#include "mrninjam/format/clap/plugin_entry.h"

extern "C" {

CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION,
    MrNinjamEntryInit,
    MrNinjamEntryDeinit,
    MrNinjamEntryGetFactory,
};

}  // extern "C"
