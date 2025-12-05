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
#include "AudioPlayer.hpp"
#include "CdReader.hpp"
#include "platform/Gpio.hpp"

#include <chrono>
#include <iostream>
#include <memory>

using bambox::BamBox;

BamBox::BamBox() {}
BamBox::~BamBox() {}

bambox::Error BamBox::config(BamBoxConfig &&cfg) {
  cfg_ = std::move(cfg);

  // TODO(qawse3dr) error check

  cd_reader_ = std::make_unique<CdReader>(cfg_.cd_mount_point);
  audio_player_ = std::make_unique<AudioPlayer>();
  gpio_ = std::make_unique<platform::Gpio>();

  gpio_->init();

  for (const auto &audio_dev : cfg_.audio_devs) {
    audio_player_->create_device(audio_dev.display_name, audio_dev.device_name,
                                 audio_dev.volume);
  }
  audio_player_->select_device(cfg_.default_audio_dev);
  return {};
}

void BamBox::go() {

  cd_reader_->load();

  cd_thread_ = std::thread([&] {
    auto disc = cd_reader_->get_disc();

    std::cout << "Playing cd: \"" << disc.title_ << "\" by: " << disc.artist_
              << std::endl;
    for (size_t i = 1; i <= disc.songs_.size(); i++) {
      std::cout << "track(" << i << "): \"" << disc.songs_[i - 1].title_
                << "\" by: " << disc.songs_[i - 1].artist_ << std::endl;
    }
    for (size_t i = 1; i <= disc.songs_.size(); i++) {
      std::cout << "Playing track(" << i << "): \"" << disc.songs_[i - 1].title_
                << "\" by: " << disc.songs_[i - 1].artist_ << std::endl;
      cd_reader_->play_track(
          i,
          [&](const std::chrono::seconds &sec, void *data, int frames) -> int {
            audio_player_->write(data, frames);
            return 0;
          });
    }
  });

  gpio_->pull_mode_set(cfg_.next_gpio, platform::Gpio::PullMode::UP);
  gpio_->pull_mode_set(cfg_.prev_gpio, platform::Gpio::PullMode::UP);
  gpio_->pull_mode_set(cfg_.play_gpio, platform::Gpio::PullMode::UP);
  gpio_->alt_func_set(cfg_.play_gpio, platform::Gpio::AltFunc::INPUT);
  gpio_->alt_func_set(cfg_.prev_gpio, platform::Gpio::AltFunc::INPUT);
  gpio_->alt_func_set(cfg_.next_gpio, platform::Gpio::AltFunc::INPUT);

  gpio_->register_irq(cfg_.next_gpio,
                      {platform::Gpio::TriggerType::FALLING_EDGE},
                      [&](unsigned int gpio, bool high) {
                        std::cout << "next" << std::endl;
                        cd_reader_->next();
                      });

  gpio_->register_irq(cfg_.prev_gpio,
                      {platform::Gpio::TriggerType::FALLING_EDGE},
                      [&](unsigned int gpio, bool high) {
                        std::cout << "prev" << std::endl;
                        cd_reader_->prev();
                      });

  gpio_->register_irq(
      cfg_.play_gpio, {platform::Gpio::TriggerType::FALLING_EDGE},
      [&](unsigned int gpio, bool high) {
        std::cout << "play" << std::endl;
        if (cd_reader_->get_state() == bambox::CdReader::State::PLAYING) {
          cd_reader_->pause();
        } else {
          cd_reader_->resume();
        }
      });
}
void BamBox::stop() { cd_thread_.join(); }