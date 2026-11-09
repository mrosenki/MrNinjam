if(NOT DEFINED NJCLIENT_SOURCE)
  message(FATAL_ERROR "NJCLIENT_SOURCE is required")
endif()

file(READ "${NJCLIENT_SOURCE}" source)
string(FIND "${source}" "void NinjamPluginNJClient::AudioProc(" begin)
string(FIND "${source}" "void NinjamPluginNJClient::BeginWorkerInterval(" end)
if(begin EQUAL -1 OR end EQUAL -1 OR end LESS_EQUAL begin)
  message(FATAL_ERROR "Could not isolate the connected AudioProc implementation")
endif()
math(EXPR length "${end} - ${begin}")
string(SUBSTRING "${source}" ${begin} ${length} callback)

set(forbidden
  "std::mutex"
  "lock_guard"
  "unique_lock"
  ".Enter("
  ".Leave("
  "AddBlock("
  "runDecode("
  "fopen"
  "fread"
  "fseek"
  "CreateNJDecoder"
  "CreateNJEncoder"
  "malloc("
  "calloc("
  "realloc("
  "free("
  "new "
  "delete "
  ".Resize("
  "sleep_for"
  ".join("
)

foreach(token IN LISTS forbidden)
  string(FIND "${callback}" "${token}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR "Connected AudioProc contains forbidden callback token: ${token}")
  endif()
endforeach()

message(STATUS "Connected AudioProc has no direct allocation, lock, codec, file, or destruction token")
