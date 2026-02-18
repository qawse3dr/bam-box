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

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

#include "BamBoxError.hpp"


namespace bambox {

struct AudioDevCfg {
  // TODO(qawse3dr) do all configs (format, ...)
  std::string display_name;
  std::string device_name;
  std::string mixer_name;
  uint8_t volume;
};

struct BamBoxConfig {
  std::string cd_mount_point = "/dev/umasscd0";
  std::string cd_cache = "bambox-info";
  std::vector<AudioDevCfg> audio_devs = {{.display_name = "Speakers", .device_name = "pcmC1D0p", .mixer_name = "controlC1", .volume = 50},
                                         {.display_name = "Headphones", .device_name = "pcmC0D0p", .mixer_name = "controlC0", .volume = 50}};
  std::string default_audio_dev = "Speakers";

  uint8_t prev_gpio = 16;
  uint8_t play_gpio = 24;
  uint8_t next_gpio = 23;

  struct {
    uint8_t clk_gpio = 4;
    uint8_t data_gpio = 3;
    uint8_t button_gpio = 2;

  } rotary_encoder;

  std::string gtk_ui_path_ = "/ca/larrycloud/bambox/ui/bambox.ui";
  std::string gtk_style_path_ = "/ca/larrycloud/bambox/ui/bambox.css";
};

}  // namespace bambox

bambox::Expected<bambox::BamBoxConfig> parse_cli(int argc, char* argv[]);