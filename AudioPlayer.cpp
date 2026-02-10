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
#include "AudioPlayer.hpp"

#include <spdlog/spdlog.h>

using bambox::AudioPlayer;

AudioPlayer::AudioPlayer() = default;

AudioPlayer::~AudioPlayer() {
  for (auto &dev : devs_) {
    if (dev.second.handle != nullptr) {
      snd_pcm_close(dev.second.handle);
    }
    if (dev.second.mixer_handle != nullptr) {
      snd_mixer_close(dev.second.mixer_handle);
    }
  }
  devs_.clear();
}

bambox::Error AudioPlayer::create_device(const AudioDevCfg &cfg) {
  AudioDevice dev = {
      .display_name = cfg.display_name, .name = cfg.device_name, .mixer = cfg.mixer_name, .volume = cfg.volume};
  snd_pcm_hw_params_t *hw_params = NULL;
  snd_pcm_sw_params_t *sw_params = NULL;
  int err = 0;
  if (snd_pcm_open(&dev.handle, dev.name.c_str(), SND_PCM_STREAM_PLAYBACK, 0) != 0) {
    spdlog::error("Failed to open audio device {}({}) with: {}", cfg.display_name, dev.name, strerror(errno));
    return {ECode::ERR_NOFILE, "snd_pcm_open"};
  }

  if ((err = snd_pcm_hw_params_malloc(&hw_params)) != 0) {
    spdlog::error("Failed to alloc hw_params: {}", snd_strerror(err));
    return {ECode::ERR_OOM, "snd_pcm_hw_params_malloc"};
  }

  if ((err = snd_pcm_hw_params_any(dev.handle, hw_params)) != 0) {
    spdlog::error("Failed to initialize hw_params for {} with: {}", cfg.display_name, snd_strerror(err));
    return {ECode::ERR_IO, "snd_pcm_hw_params_any"};
  }

  if ((err = snd_pcm_hw_params_set_access(dev.handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) != 0) {
    spdlog::error("Failed to access type for {} with: {}", cfg.display_name, snd_strerror(err));
    return {ECode::ERR_UNKNOWN, "snd_pcm_hw_params_set_access"};
  }

  if ((err = snd_pcm_hw_params_set_format(dev.handle, hw_params, SND_PCM_FORMAT_S16_LE)) != 0) {
    spdlog::error("Failed to access type for {} with: {}", cfg.display_name, snd_strerror(err));
    return {ECode::ERR_UNKNOWN, "snd_pcm_hw_params_set_access"};
  }

  unsigned int rate = 44100;
  if ((err = snd_pcm_hw_params_set_rate_near(dev.handle, hw_params, &rate, NULL)) != 0) {
    spdlog::error("Cannot set sample rate for {} with:{}", cfg.display_name, snd_strerror(err));
    return {ECode::ERR_UNKNOWN, "snd_pcm_hw_params_set_rate_near"};
  }

  if ((err = snd_pcm_hw_params_set_channels(dev.handle, hw_params, 2)) < 0) {
    spdlog::error("Cannot set channel count for {} with:{}", cfg.display_name, snd_strerror(err));
    return {ECode::ERR_UNKNOWN, "snd_pcm_hw_params_set_channels"};
  }

  // This is done to limit the how far away the buffer is vs what is being written into the song.
  int dir = 0;
  uint buffer_time = 250000; // 250 ms
  if ((err = snd_pcm_hw_params_set_buffer_time_near(dev.handle, hw_params, &buffer_time, &dir)) < 0) {
    spdlog::error("Cannot set channel count for {} with:{}", cfg.display_name, snd_strerror(err));
    return {ECode::ERR_UNKNOWN, "snd_pcm_hw_params_set_channels"};
  }

  if ((err = snd_pcm_hw_params(dev.handle, hw_params)) != 0) {
    spdlog::error("Cannot set parameters {} with:{}", cfg.display_name, snd_strerror(err));
    return {ECode::ERR_UNKNOWN, "snd_pcm_hw_params"};
  }
  snd_pcm_hw_params_free(hw_params);

  snd_pcm_sw_params_malloc(&sw_params);
  snd_pcm_sw_params_current(dev.handle, sw_params);
  snd_pcm_sw_params_set_start_threshold(dev.handle, sw_params,
                                        1);  // start after 1 frame
  snd_pcm_sw_params_set_avail_min(dev.handle, sw_params, 1);

  snd_pcm_sw_params(dev.handle, sw_params);
  snd_pcm_sw_params_free(sw_params);

  if ((err = snd_pcm_prepare(dev.handle)) != 0) {
    spdlog::error("Cannot prepare audio interface {} with:{}", cfg.display_name, snd_strerror(err));
    return {ECode::ERR_UNKNOWN, "snd_pcm_prepare"};
  }

  // Get volume for dev
  snd_mixer_selem_id_t *sid = NULL;

  const char *card = cfg.mixer_name.c_str();  // or "hw:0"
  const char *selem_name = "PCM Mixer";       // adjust for your device
  // --- Open mixer ---
  if (snd_mixer_open(&dev.mixer_handle, 0) < 0) {
    spdlog::warn("Failed to open mixer {}", card);
    return {bambox::ECode::ERR_NOFILE, "Failed to open mixer"};
  }
  if (snd_mixer_attach(dev.mixer_handle, card) < 0) {
    spdlog::warn("Failed to open card {}", card);
    return {bambox::ECode::ERR_NOFILE, "Failed to open mixer"};
  }
  if (snd_mixer_selem_register(dev.mixer_handle, NULL, NULL) < 0) {
    spdlog::warn("snd_mixer_selem_register", card);
    return {bambox::ECode::ERR_NOFILE, "snd_mixer_selem_register"};
  }
  if (snd_mixer_load(dev.mixer_handle) < 0) {
    spdlog::warn("snd_mixer_load", card);
    return {bambox::ECode::ERR_NOFILE, "snd_mixer_selem_register"};
  }

  snd_mixer_selem_id_alloca(&sid);
  snd_mixer_selem_id_set_index(sid, 0);
  snd_mixer_selem_id_set_name(sid, selem_name);

  dev.volume_element = snd_mixer_find_selem(dev.mixer_handle, sid);
  if (!dev.volume_element) {
    spdlog::warn("failed to find selem", card);
    return {bambox::ECode::ERR_NOFILE, "snd_mixer_selem_register"};
  }

  devs_.insert({cfg.display_name, dev});
  current_dev_ = &devs_[cfg.display_name];

  set_volume(cfg.volume);

  return {};
}

int AudioPlayer::write(void *data, int frames) {
  if (current_dev_ == nullptr) {
    return -1;
  }
  // TODO this may return the amount written.
  auto ret = snd_pcm_writei(current_dev_->handle, data, frames);

  // This is a workaround for being paused.
  // I really need to update it to not pause on overruns
  if (ret < 0) {
    snd_pcm_recover(current_dev_->handle, -EPIPE, 1);
  }
  return 0;
}

int AudioPlayer::select_device(const std::string &dev_name) {
  auto itr = devs_.find(dev_name);
  if (itr == devs_.end()) {
    return -1;
  }

  current_dev_ = &itr->second;
  return 0;
}

bambox::Error AudioPlayer::pause(bool resume) {
  // pause takes the reverse of resume.
  snd_pcm_pause(current_dev_->handle, (resume) ? 1 : 0);
  snd_pcm_recover(current_dev_->handle, -EPIPE, 1);
  return {};
}

bambox::Error AudioPlayer::set_volume(uint8_t percent) {
  spdlog::info("Setting volume to {}%", percent);

  long min, max;
  snd_mixer_selem_get_playback_volume_range(current_dev_->volume_element, &min, &max);

  percent = std::max(0, std::min(100, static_cast<int>(percent)));
  long vol = min + (percent * (max - min)) / 100;
  snd_mixer_selem_set_playback_volume_all(current_dev_->volume_element, vol);

  spdlog::info("volume set to {}%", percent);
  current_dev_->volume = percent;
  return {};
}

std::vector<std::string> AudioPlayer::get_device_names() const {
  std::vector<std::string> names;
  for (auto dev : devs_) {
    names.push_back(dev.first);
  }
  return names;
}

std::string AudioPlayer::get_selected_device_name() const {
  return current_dev_->display_name;
}
