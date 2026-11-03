#pragma once

#include "njclient.h"

// Small project-owned extension of the pinned upstream client.  NINJAM's
// channel list protocol does not carry mono/stereo metadata; that information
// becomes available only after the OGG decoder has read the stream header.
class NinjamPluginNJClient final : public NJClient {
 public:
  int GetUserChannelCount(int user_index, int channel_index);
};
