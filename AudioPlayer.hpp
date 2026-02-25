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
#include <alsa/asoundlib.h>

#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#include "AudioSink.hpp"
#include "BamBoxConfig.hpp"
#include "BamBoxError.hpp"

namespace bambox {
class AudioPlayer : public AudioSink {
 public:
  struct AudioDevice {
    std::string display_name = {};
    std::string name = {};
    std::string mixer = {};
    snd_pcm_t *handle = NULL;
    snd_mixer_t *mixer_handle = NULL;
    snd_mixer_elem_t *volume_element = NULL;
    uint8_t volume = 100;
  };

 private:
  AudioDevice *current_dev_ = nullptr;
  std::unordered_map<std::string, AudioDevice> devs_{};

 public:
  /// TODO change to config
  AudioPlayer();
  ~AudioPlayer();

  /**
   * Creates an audio device and adds to the list of devices
   *
   * TODO: it should take in config instead of just the name
   */
  Error create_device(const AudioDevCfg &cfg);

  /**
   * Sets the current audio device.
   */
  int select_device(const std::string &dev_name);
  Error set_volume(uint8_t volume_percent);
  uint8_t get_volume() const { return current_dev_->volume; }

  /**
   * @brief Get a list of all configured devices by their human readable name
   */
  std::vector<std::string> get_device_names() const;

  /**
   * @brief Get the human readable name of the current device
   */
  std::string get_selected_device_name() const;

  /**
   * @brief Pauses playback of whatever is currently cached into the stream.
   * This is needed to avoid underruns in the buffer causing it to lock up.
   */
  Error pause(bool resume);

  /**
   * @brief Writes data into the audio buffer.
   *
   * @param data Data to be written into the audio stream
   * @param frames amount of frames contained in the data. Note this is not the same as the length
   */
  int write(void *data, int frames) override;
};

}  // namespace bambox
