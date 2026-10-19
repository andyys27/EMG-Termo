#pragma once
#include <stddef.h>
#include <stdint.h>
#include "protocol.h"
#include "sample_block.h"

constexpr size_t MAX_FRAME_BYTES = proto::frameSize(cfg::BLOCK_SAMPLES, cfg::N_CH);

// Serializa un bloque en una trama UDP (encabezado + datos + CRC16). Devuelve el tamaño escrito, o 0 si no cabe.
size_t packetize(const SampleBlock& blk, uint8_t* out, size_t capacity);
