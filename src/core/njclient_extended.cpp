#include "ninjamplugin/core/njclient_extended.h"

#include <limits>

// Build upstream NJClient in this translation unit so the project-owned
// subclass below can inspect the decoder objects, whose definitions are kept
// private to njclient.cpp by upstream.
#include <ninjam/njclient.cpp>

bool NinjamPluginNJClient::PrepareAudio(const std::size_t maximum_frame_count) {
  constexpr std::size_t kBytesPerMonoFrame = sizeof(float);
  constexpr std::size_t kBytesPerStereoFrame = sizeof(float) * 2U;
  if (maximum_frame_count == 0U ||
      maximum_frame_count >
          static_cast<std::size_t>(std::numeric_limits<int>::max()) /
              kBytesPerStereoFrame) {
    return false;
  }

  const int mono_bytes =
      static_cast<int>(maximum_frame_count * kBytesPerMonoFrame);
  const int stereo_bytes =
      static_cast<int>(maximum_frame_count * kBytesPerStereoFrame);
  tmpblock.Resize(mono_bytes, false);
  remote_mix_tmpblock.Resize(stereo_bytes, false);
  return tmpblock.GetSize() >= mono_bytes &&
         remote_mix_tmpblock.GetSize() >= stereo_bytes;
}

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
