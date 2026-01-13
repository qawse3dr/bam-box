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

#include "BamBox.hpp"

#include <spdlog/spdlog.h>
#include <sys/dcmd_cam.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>

#include "AudioPlayer.hpp"
#include "BamBoxError.hpp"
#include "CdReader.hpp"
#include "platform/Gpio.hpp"

using bambox::BamBox;

BamBox::BamBox() {}
BamBox::~BamBox() {}

bambox::Error BamBox::config(BamBoxConfig &&cfg) {
  spdlog::info("Configuring...");
  cfg_ = std::move(cfg);
  cd_reader_ = std::make_unique<CdReader>(cfg_.cd_mount_point);
  audio_player_ = std::make_unique<AudioPlayer>();
  gpio_ = std::make_shared<platform::Gpio>();
  lcd_display_ = std::make_unique<LcdDisplay>(gpio_);
  ui_ = std::make_unique<BamBoxUI>();

  spdlog::info("Configuring GPIO");
  if (gpio_->init() != 0) {
    return {ECode::ERR_IO, "Failed it initialize GPIO are you root?"};
  }

  // Must be done after GPIO as it makes use of it
  spdlog::info("Configuring Display");
  auto res = lcd_display_->init();
  if (res.is_error()) {
    return {};
  }

  spdlog::info("Configuring Audio");
  for (const auto &audio_dev : cfg_.audio_devs) {
    audio_player_->create_device(audio_dev);
  }
  audio_player_->select_device(cfg_.default_audio_dev);

  res = ui_->init();
  if (res.is_error()) {
    return {};
  }

  return {};
}

void BamBox::cd_player_loop() {
  while (1) {
    {
      std::lock_guard<std::mutex> lk(mtx_);
      state_ = State::NO_DISC;
    }
    spdlog::info("Waiting for CD...");
    ui_->set_cd_info({});
    while (cd_reader_->wait_for_disc().is_error())
      ;

    {
      std::lock_guard<std::mutex> lk(mtx_);
      state_ = State::LOADING;
    }
    auto res = cd_reader_->load();
    if (res.is_error()) {
      spdlog::warn("Failed to load CD with: {}", res.str());
      continue;
    }

    {
      std::lock_guard<std::mutex> lk(mtx_);
      state_ = State::PLAYING;
    }
    auto disc = cd_reader_->get_disc();
    spdlog::info("Playing cd: \"{}\" by: \"{}\"", disc.title_, disc.artist_);

    cd_reader_->set_position(1);
    ui_->set_cd_info({disc});
    while (1) {
      auto track = disc.songs_[cd_reader_->get_track_number() - 1];
      spdlog::info("Playing track({}): \"{}\" by: {}", track.track_num_, track.title_, track.artist_);
      ui_->set_song_info(track);
      {
        std::unique_lock<std::mutex> lk(mtx_);
        // If there was a seek request update the song info.
        if (seek_request_) {
          seek_request_ = false;
          continue;  // Update track info.
        }

        if (is_paused_) {
          spdlog::info("Song paused... cd thread going to sleep");
          cv_.wait(lk, [&]() { return state_ != State::PLAYING || !is_paused_ || seek_request_; });
          continue;  // continue to update track in case it changed when we were sleeping
        }
      }

      auto res = cd_reader_->play([&](const std::chrono::seconds &sec, void *data, int frames) -> int {
        ui_->set_set_track_time(sec);
        audio_player_->write(data, frames);
        return 0;
      });

      {
        std::unique_lock<std::mutex> lk(mtx_);
        if (res.is_error()) {
          spdlog::warn("Failed to play cd with: {}", res.str());
          break;
        } else if (!is_paused_ && !seek_request_) {
          lk.unlock();
          res = next();
          if (res.is_error()) {
            spdlog::warn("Failed too play next songs with: {}", res.str());
          }
        }
      }
    }
  }
}

bambox::Error BamBox::go() {
  cd_thread_ = std::thread([this] { this->cd_player_loop(); });

  auto res = lcd_display_->go();
  if (res.is_error()) {
    return res;
  }

  gpio_->pull_mode_set(cfg_.next_gpio, platform::Gpio::PullMode::UP);
  gpio_->pull_mode_set(cfg_.prev_gpio, platform::Gpio::PullMode::UP);
  gpio_->pull_mode_set(cfg_.play_gpio, platform::Gpio::PullMode::UP);
  gpio_->alt_func_set(cfg_.play_gpio, platform::Gpio::AltFunc::INPUT);
  gpio_->alt_func_set(cfg_.prev_gpio, platform::Gpio::AltFunc::INPUT);
  gpio_->alt_func_set(cfg_.next_gpio, platform::Gpio::AltFunc::INPUT);

  gpio_->pull_mode_set(cfg_.rotary_encoder.button_gpio, platform::Gpio::PullMode::UP);
  gpio_->pull_mode_set(cfg_.rotary_encoder.clk_gpio, platform::Gpio::PullMode::UP);
  gpio_->pull_mode_set(cfg_.rotary_encoder.data_gpio, platform::Gpio::PullMode::UP);
  gpio_->alt_func_set(cfg_.rotary_encoder.button_gpio, platform::Gpio::AltFunc::INPUT);
  gpio_->alt_func_set(cfg_.rotary_encoder.clk_gpio, platform::Gpio::AltFunc::INPUT);
  gpio_->alt_func_set(cfg_.rotary_encoder.data_gpio, platform::Gpio::AltFunc::INPUT);

  gpio_->register_irq(
      cfg_.next_gpio, {platform::Gpio::TriggerType::FALLING_EDGE},
      [&](unsigned int gpio, bool high) {
        spdlog::info("next song");
        next();
      },
      std::chrono::milliseconds(500));

  gpio_->register_irq(
      cfg_.prev_gpio, {platform::Gpio::TriggerType::FALLING_EDGE},
      [&](unsigned int gpio, bool high) {
        spdlog::info("previous song");
        prev();
      },
      std::chrono::milliseconds(500));

  gpio_->register_irq(
      cfg_.play_gpio, {platform::Gpio::TriggerType::FALLING_EDGE},
      [&](unsigned int gpio, bool high) {
        bambox::Error res;
        if (!is_paused_) {
          spdlog::info("pausing song");
          res = pause();
        } else {
          spdlog::info("resuming song");
          res = resume();
        }
        if (res.is_error()) {
          spdlog::warn("Failed to play/pause with: {}", res.str());
        }
      },
      std::chrono::milliseconds(500));

  gpio_->register_irq(cfg_.rotary_encoder.button_gpio, {platform::Gpio::TriggerType::RISING_EDGE},
                      [&](unsigned int gpio, bool high) { spdlog::info("Rotary Encoder pressed"); });

  gpio_->register_irq(cfg_.rotary_encoder.clk_gpio,
                      {platform::Gpio::TriggerType::RISING_EDGE, platform::Gpio::TriggerType::FALLING_EDGE},
                      [&](unsigned int gpio, bool high) {
                        static bool old_state = false;

                        if (high != old_state) {
                          bool dt = gpio_->level_get(cfg_.rotary_encoder.data_gpio) != 0;
                          if (dt) { // Only do one of the bumps to avoid incrementing twice per turn.
                            if (high == dt) {
                              ui_->input_right();
                            } else {
                              ui_->input_left();
                            }
                            // int vol = static_cast<int>(audio_player_->get_volume()) + ((high == dt) ? 1 : -1);
                            // audio_player_->set_volume(std::max(vol, 0));
                          }
                        }
                        old_state = high;
                      });

  // Start UI
  return ui_->go();
}

bambox::Error BamBox::pause() {
  std::lock_guard<std::mutex> lk(mtx_);
  if (is_paused_ || (state_ != State::PLAYING)) {
    return {ECode::ERR_AGAIN, "pause(): Already paused or not playing."};
  }
  audio_player_->pause(false);
  cd_reader_->stop();
  is_paused_ = true;
  return {};
}

bambox::Error BamBox::resume() {
  std::lock_guard<std::mutex> lk(mtx_);
  if (!is_paused_ || (state_ != State::PLAYING)) {
    return {ECode::ERR_AGAIN, "resume(): Already playing or not disc."};
  }

  is_paused_ = false;
  audio_player_->pause(true);
  cv_.notify_all();
  return {};
}

bambox::Error BamBox::prev() {
  std::lock_guard<std::mutex> lk(mtx_);
  if (state_ != State::PLAYING) {
    return {ECode::ERR_INVAL_STATE, "Invalid state can't select prev track."};
  }

  if (!is_paused_) {
    cd_reader_->stop();
  }
  auto current_lba = cd_reader_->get_track_current_lba();
  auto start_lba = cd_reader_->get_track_start_lba();
  std::uint32_t three_sec_lba = MSF2LBA(0, 10, 0);
  std::int32_t track_num = cd_reader_->get_track_number();
  if ((current_lba - start_lba) <= three_sec_lba) {  // Play Previous track.
    track_num--;
    if (track_num <= 0) {
      track_num = cd_reader_->get_disc().songs_.size();
    }
  }  // Restart track

  auto res = cd_reader_->set_position(track_num);
  seek_request_ = true;
  cv_.notify_all();
  return res;
}

bambox::Error BamBox::next() {
  std::lock_guard<std::mutex> lk(mtx_);
  if (state_ != State::PLAYING) {
    return {ECode::ERR_AGAIN, "Invalid state can't select prev track."};
  }

  auto track_num = cd_reader_->get_track_number() + 1;
  if (track_num > cd_reader_->get_disc().songs_.size()) {
    track_num = 1;
  }

  // We must stop the song
  if (!is_paused_) {
    cd_reader_->stop();
  }
  auto res = cd_reader_->set_position(track_num);
  seek_request_ = true;
  cv_.notify_all();
  return res;
}

void BamBox::stop() {
  state_ = State::EXIT;
  cv_.notify_all();
  cd_thread_.join();
}
