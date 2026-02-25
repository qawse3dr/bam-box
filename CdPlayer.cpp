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

#include "CdPlayer.hpp"

#include <spdlog/spdlog.h>

using bambox::CdPlayer;
using bambox::Error;

CdPlayer::CdPlayer(const std::shared_ptr<CdReader>& reader, std::shared_ptr<AudioPlayer>& audio_player,
                   EventCB event_cb)
    : reader_(reader), audio_player_(audio_player), event_cb_(event_cb) {
}

Error CdPlayer::start() {
  cd_loader_loop_ = std::thread(&CdPlayer::cd_loader_loop, this);
  return {};
}

Error CdPlayer::load() {
  Error res;
  do {
    std::unique_lock<std::mutex> lk(mtx_);
    res = reader_->load();
    spdlog::info("load result: {}", res.str());
  } while (res.is_error());

  event_cb_(Event::CD_LOADED, reader_->get_disc());
  return res;
}

Error CdPlayer::play() {
  spdlog::info("CdPlayer::play()");
  std::lock_guard<std::mutex> lk(mtx_);
  if (state_ != State::STOPPED) {
    spdlog::info("CdPlayer::play err()");

    return {ECode::ERR_INVAL_STATE, "CdPlayer cannot be started, already running"};
  }
  state_ = State::PLAYING;
  cd_reader_loop_ = std::thread(&CdPlayer::cd_reader_loop, this);
  spdlog::info("CdPlayer::play rtn()");

  return {};
}

Error CdPlayer::pause() {
  std::unique_lock<std::mutex> lk(mtx_);
  if (state_ != State::PLAYING) {
    return {ECode::ERR_INVAL_STATE, "CdPlayer cannot be stopped, not running"};
  }
  state_ = State::STOPPING;

  // Briefly unlock to join the cd loop.
  lk.unlock();
  cd_reader_loop_.join();
  lk.lock();
  state_ = State::STOPPED;
  return {};
}

void CdPlayer::cd_reader_loop() {
  CdReader::AudioData data = {.frames = 0};
  std::chrono::seconds prev_sec(1000000000);
  Error err{};
  while (1) {
    // Lock when reading from the cd player to avoid threading issues.
    {
      std::lock_guard<std::mutex> lk(mtx_);
      // Stop requested.
      if (state_ == State::STOPPING) {
        return;
      }
      err = reader_->read(data);
    }
    // No read the message outside of the lock checking the contents for any callbacks.

    if (data.frames == EOF) {
      // notify the track ended but keep reading, this assums that the event_cb_ will update the reader
      EventData event_data(0);
      event_cb_(Event::CD_TRACK_ENDED, event_data);
      continue;
    }

    if (err.is_error()) {
      EventData event_data(err);
      event_cb_(Event::CD_EJECTED, event_data);
      return;
    }

    auto ret = audio_player_->write(data.data.data(), data.frames);
    if (ret != 0) {
      spdlog::warn("Failed to write audio: %d", ret);
    }

    // Update time after write as it is more important. Also only update if seconds actually changed, to avoid spam.
    if (prev_sec != data.ts) {
      event_cb_(Event::CD_TIME_UPDATE, data.ts);
      prev_sec = data.ts;
    }
  }
}

void CdPlayer::cd_loader_loop() {
  EventData event_data(0);
  event_cb_(CdPlayer::Event::STARTUP, event_data);
  // while(1) {}
}
