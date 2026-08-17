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
#pragma once

#include <FLAC++/encoder.h>
#include <FLAC++/metadata.h>

#include "AudioSink.hpp"
#include "CdReader.hpp"

namespace bambox {
class FlacWriter : public AudioSink {
 public:
  /**
   * @param path  file to write the encoded track to
   * @param cd    disc the track belongs to, used for the tags
   * @param track CD track number (not an index into cd.songs_)
   */
  FlacWriter(const std::string& path, const CdReader::CD& cd, int track);
  FlacWriter(const FlacWriter&) = delete;
  FlacWriter& operator=(const FlacWriter&) = delete;

  bool is_valid();
  int write(void* data, int frames) override;
  Error finish();

 private:
  FLAC::Encoder::File fp;

  // The encoder only stores pointers to these, so they have to outlive init()
  // and stay put until finish() has written the stream out.
  FLAC::Metadata::VorbisComment comments_{};
  FLAC::Metadata::Padding padding_{};
  FLAC::Metadata::Prototype* meta_[2] = {&comments_, &padding_};
};
}  // namespace bambox
