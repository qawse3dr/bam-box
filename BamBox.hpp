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

#include "BamBoxError.hpp"
#include "AudioPlayer.hpp"
#include "BamBoxConfig.hpp"
#include "CdReader.hpp"
#include "platform/Gpio.hpp"

#include <memory>
#include <thread>

namespace bambox {
class BamBox {
public:
  BamBox();
  ~BamBox();

  Error config(BamBoxConfig &&cfg);
  void go();
  void stop();

private:
  std::unique_ptr<CdReader> cd_reader_{};
  std::unique_ptr<AudioPlayer> audio_player_{};
  std::unique_ptr<platform::Gpio> gpio_{};
  BamBoxConfig cfg_;
  std::thread cd_thread_{};
};
} // namespace bambox