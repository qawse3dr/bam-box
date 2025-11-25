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
#include "audioplayer.hpp"


using bambox::AudioPlayer;

AudioPlayer::AudioPlayer() = default;

AudioPlayer::~AudioPlayer() {
  for (auto &dev : devs_) {
    if (dev.second.handle == nullptr) {
      snd_pcm_close(dev.second.handle);
    }
  }
  devs_.clear();
}

int AudioPlayer::create_device(const std::string &dev_name) {
  AudioDevice dev;
  snd_pcm_hw_params_t *hw_params = NULL;
  snd_pcm_sw_params_t *sw_params = NULL;
  int err = 0;
  if (snd_pcm_open(&dev.handle, dev_name.c_str(), SND_PCM_STREAM_PLAYBACK, 0) != 0) {
    perror("snd_pcm_open");
    return -1;
  }

  if ((err = snd_pcm_hw_params_malloc(&hw_params)) != 0) {
    fprintf(stderr, "cannot allocate hardware parameter structure (%s)\n",
            snd_strerror(err));
    return -1;
  }

  if ((err = snd_pcm_hw_params_any(dev.handle, hw_params)) != 0) {
    fprintf(stderr, "cannot initialize hardware parameter structure (%s)\n",
            snd_strerror(err));
    return -1;
  }

  if ((err = snd_pcm_hw_params_set_access(
           dev.handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) != 0) {
    fprintf(stderr, "cannot set access type (%s)\n", snd_strerror(err));
    return -1;
  }

  if ((err = snd_pcm_hw_params_set_format(dev.handle, hw_params,
                                          SND_PCM_FORMAT_S16_LE)) != 0) {
    fprintf(stderr, "cannot set sample format (%s)\n", snd_strerror(err));
    return -1;
  }

  unsigned int rate = 44100;
  if ((err = snd_pcm_hw_params_set_rate_near(dev.handle, hw_params, &rate,
                                             NULL)) != 0) {
    fprintf(stderr, "cannot set sample rate (%s)\n", snd_strerror(err));
    return -1;
  }

  if ((err = snd_pcm_hw_params_set_channels(dev.handle, hw_params, 2)) < 0) {
    fprintf(stderr, "cannot set channel count (%s)\n", snd_strerror(err));
    return -1;
  }
  if ((err = snd_pcm_hw_params(dev.handle, hw_params)) != 0) {
    fprintf(stderr, "cannot set parameters (%s)\n", snd_strerror(err));
    return -1;
  }
  snd_pcm_hw_params_free(hw_params);

  snd_pcm_sw_params_malloc(&sw_params);
  snd_pcm_sw_params_current(dev.handle, sw_params);
  snd_pcm_sw_params_set_start_threshold(dev.handle, sw_params,
                                        1); // start after 1 frame
  snd_pcm_sw_params_set_avail_min(dev.handle, sw_params, 1);

  snd_pcm_sw_params(dev.handle, sw_params);
  snd_pcm_sw_params_free(sw_params);

  if ((err = snd_pcm_prepare(dev.handle)) != 0) {
    fprintf(stderr, "cannot prepare audio interface for use (%s)\n",
            snd_strerror(err));
    exit(1);
  }

  devs_.insert({dev_name, dev});
  current_dev_ = &devs_[dev_name];

  return 0;
}

int AudioPlayer::write(void *data, int frames) {
  if (current_dev_ == nullptr) {
    return -1;
  }
  // TODO this may return the amount written.
  return snd_pcm_writei(current_dev_->handle, data, frames);
}

int AudioPlayer::select_device(const std::string &dev_name) {
  auto itr = devs_.find(dev_name);
  if (itr == devs_.end()) {
    return -1;
  }

  current_dev_ = &itr->second;
  return 0;
}
