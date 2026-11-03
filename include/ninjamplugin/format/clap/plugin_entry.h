#pragma once

#include <clap/clap.h>

extern "C" {

bool CLAP_ABI NinjamPluginEntryInit(const char* plugin_path);
void CLAP_ABI NinjamPluginEntryDeinit();
const void* CLAP_ABI NinjamPluginEntryGetFactory(const char* factory_id);

}  // extern "C"
