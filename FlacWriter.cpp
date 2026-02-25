/*
 * Copyright (C) 2025 Larry Milne (https://www.larrycloud.ca)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "FlacWriter.hpp"

#include "FLAC++/metadata.h"

#include <spdlog/spdlog.h>

using bambox::FlacWriter;

FlacWriter::FlacWriter(const std::string& path, CdReader::CD&, int track) {
  fp.init(path);
  fp.set_compression_level(5);
  fp.set_bits_per_sample(16);
  fp.set_channels(2);
  fp.set_sample_rate(44100);
}

bool FlacWriter::is_valid() { return fp.is_valid(); }

int FlacWriter::write(void* data, int frames) {
  // convert PCM data into flac array
  FLAC__int32 pcm[frames * 2];
  uint8_t* buffer = (uint8_t*)data;
  for (int i = 0; i < frames * 2; i++) {
    pcm[i] = (int16_t)(buffer[2*i] | (buffer[2*i + 1] << 8));
  }

  if (!fp.process_interleaved(pcm, frames)) {
    spdlog::warn("error writing data {}", FLAC__StreamEncoderStateString[fp.get_state()]);
  }
  return 0;
}

bambox::Error FlacWriter::finish() {
  fp.finish();
  return {};
}
