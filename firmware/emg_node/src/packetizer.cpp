#include "packetizer.h"
#include <string.h>

size_t packetize(const SampleBlock& blk, uint8_t* out, size_t capacity) {
  const size_t n = blk.n;
  const size_t total = proto::frameSize(n, cfg::N_CH);
  if (n == 0 || n > cfg::BLOCK_SAMPLES || capacity < total) return 0;

  proto::FrameHeader h;
  h.magic[0] = proto::MAGIC0;
  h.magic[1] = proto::MAGIC1;
  h.version = proto::VERSION;
  h.device_id = cfg::DEVICE_ID;
  h.seq = blk.seq;
  h.t_us = blk.t_us;
  h.n_samples = static_cast<uint16_t>(n);
  h.n_channels = cfg::N_CH;
  h.flags = blk.flags;

  memcpy(out, &h, sizeof(h));
  memcpy(out + sizeof(h), &blk.data[0][0], n * cfg::N_CH * sizeof(int32_t));

  const size_t body = sizeof(h) + n * cfg::N_CH * sizeof(int32_t);
  const uint16_t crc = proto::crc16_ccitt(out, body);
  memcpy(out + body, &crc, sizeof(crc));   // little-endian
  return total;
}
