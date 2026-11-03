#include "ninjamplugin/core/njclient_extended.h"

// Build upstream NJClient in this translation unit so the project-owned
// subclass below can inspect the decoder objects, whose definitions are kept
// private to njclient.cpp by upstream.
#include <njclient.cpp>

int NinjamPluginNJClient::GetUserChannelCount(const int user_index,
                                              const int channel_index) {
  WDL_MutexLock lock(&m_remotechannel_rd_mutex);
  if (user_index < 0 || user_index >= m_remoteusers.GetSize() ||
      channel_index < 0 || channel_index >= MAX_USER_CHANNELS) {
    return 0;
  }
  RemoteUser* user = m_remoteusers.Get(user_index);
  if (user == nullptr || !(user->chanpresentmask & (1 << channel_index))) {
    return 0;
  }
  RemoteUser_Channel& channel = user->channels[channel_index];
  DecodeState* decoders[] = {channel.ds, channel.next_ds[0], channel.next_ds[1]};
  for (DecodeState* decoder : decoders) {
    if (decoder == nullptr || decoder->decode_codec == nullptr) continue;
    const int count = decoder->decode_codec->GetNumChannels();
    if (count > 0) return count > 1 ? 2 : 1;
  }
  return 0;
}
