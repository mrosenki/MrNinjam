#pragma once

#include <clap/clap.h>

extern "C" {

bool CLAP_ABI MrNinjamEntryInit(const char* plugin_path);
void CLAP_ABI MrNinjamEntryDeinit();
const void* CLAP_ABI MrNinjamEntryGetFactory(const char* factory_id);

}  // extern "C"
