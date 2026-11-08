if(NOT DEFINED CLAP_ENTRY OR NOT DEFINED BRIDGE_SOURCE OR
   NOT DEFINED RECORDER_SOURCE)
  message(FATAL_ERROR "CLAP_ENTRY, BRIDGE_SOURCE, and RECORDER_SOURCE are required")
endif()

function(check_region file_path start_marker end_marker region_name)
  file(READ "${file_path}" contents)
  string(FIND "${contents}" "${start_marker}" start_offset)
  string(FIND "${contents}" "${end_marker}" end_offset)
  if(start_offset EQUAL -1 OR end_offset EQUAL -1 OR
     end_offset LESS_EQUAL start_offset)
    message(FATAL_ERROR "Could not locate ${region_name} in ${file_path}")
  endif()
  math(EXPR region_length "${end_offset} - ${start_offset}")
  string(SUBSTRING "${contents}" ${start_offset} ${region_length} region)

  foreach(forbidden IN ITEMS
      "DebugLog(" "DebugLogf(" ".Stop(" ".Wait(" "join(" "lock_guard"
      "filesystem" "ClearSessionFeedState(" "fopen(" "fclose(")
    string(FIND "${region}" "${forbidden}" forbidden_offset)
    if(NOT forbidden_offset EQUAL -1)
      message(FATAL_ERROR
        "${region_name} contains forbidden real-time operation '${forbidden}'")
    endif()
  endforeach()
endfunction()

check_region("${CLAP_ENTRY}"
  "bool CLAP_ABI PluginStartProcessing"
  "void CLAP_ABI PluginStopProcessing"
  "CLAP PluginStartProcessing")
check_region("${CLAP_ENTRY}"
  "void CLAP_ABI PluginStopProcessing"
  "void CLAP_ABI PluginReset"
  "CLAP PluginStopProcessing")
check_region("${CLAP_ENTRY}"
  "void CLAP_ABI PluginReset"
  "clap_process_status CLAP_ABI PluginProcess"
  "CLAP PluginReset")

check_region("${BRIDGE_SOURCE}"
  "bool NinjamBridge::StartProcessing"
  "void NinjamBridge::StopProcessing"
  "NinjamBridge::StartProcessing")
check_region("${BRIDGE_SOURCE}"
  "void NinjamBridge::StopProcessing"
  "void NinjamBridge::Reset"
  "NinjamBridge::StopProcessing")
check_region("${BRIDGE_SOURCE}"
  "void NinjamBridge::Reset"
  "void NinjamBridge::MainThreadTick"
  "NinjamBridge::Reset")
check_region("${RECORDER_SOURCE}"
  "void SessionCaptureRecorder::RequestStop"
  "bool SessionCaptureRecorder::IsActive"
  "SessionCaptureRecorder::RequestStop")

message(STATUS "Real-time lifecycle callback source check passed")
