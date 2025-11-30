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
#include <chrono>
#include <iostream>
#include <thread>

#include "audioplayer.hpp"
#include "cdreader.hpp"

#include <sys/mman.h>

volatile uint32_t *gpio_base = NULL;

#define BCM2711_GPIO_BASE 0xfe200000
#define BLOCK_SIZE (4 * 1024)

#define GPIO_SET 7
#define GPIO_CLR 10
#define GPIO_READ 13
#define GPIO_PULL_SET 57
#define GPIO_PULL_NONE 0
#define PULL_DOWN 2
#define PULL_UP 1

#define PREV_PIN 16
#define PLAY_PIN 24
#define NEXT_PIN 23

void write_pin(int pin, int value) {
  if (value) {
    *(gpio_base + GPIO_SET) = (uint32_t)(0x1 << pin);
  } else {
    *(gpio_base + GPIO_CLR) = (uint32_t)(0x1 << pin);
  }
}

int read_pin(unsigned pin) { return (*(gpio_base + GPIO_READ) >> pin) & 0x1; }

void set_pin_mode(int pin, int mode) {
  uint32_t reg = pin / 10;
  const uint32_t sel = pin % 10;
  uint32_t val;
  val = *(gpio_base + reg);
  val &= ~(0x7 << (3 * sel));
  val |= (mode & 0x7) << (3 * sel);
  *(gpio_base + reg) = val;
}

void set_pin_pull(int pin, unsigned int pull_mode) {
  uint32_t pullreg = (GPIO_PULL_SET + (pin >> 4));
  const uint32_t pullshift = (uint32_t)((pin & 0xf) << 1);
  unsigned int pullbits = *(gpio_base + pullreg);
  pullbits &= ~(3 << pullshift);
  pullbits |= (pull_mode << pullshift);
  *(gpio_base + pullreg) = pullbits;
}

int main() {
  std::cout << "Welcome to bambox" << std::endl;
  bambox::CdReader cd("/dev/umasscd0");
  bambox::AudioPlayer audio_player;

  audio_player.create_device("pcmC1D0p");

  cd.load();

  auto disc = cd.get_disc();

  std::cout << "Playing cd: \"" << disc.title_ << "\" by: " << disc.artist_
            << std::endl;
  for (size_t i = 1; i <= disc.songs_.size(); i++) {
    std::cout << "track(" << i << "): \"" << disc.songs_[i - 1].title_
              << "\" by: " << disc.songs_[i - 1].artist_ << std::endl;
  }

  std::thread cd_thread([&] {
    for (size_t i = 1; i <= disc.songs_.size(); i++) {
      std::cout << "Playing track(" << i << "): \"" << disc.songs_[i - 1].title_
                << "\" by: " << disc.songs_[i - 1].artist_ << std::endl;
      cd.play_track(
          i,
          [&](const std::chrono::seconds &sec, void *data, int frames) -> int {
            audio_player.write(data, frames);
            return 0;
          });
    }
  });

  gpio_base = (volatile uint32_t *)mmap_device_memory(
      NULL, BLOCK_SIZE, PROT_NOCACHE | PROT_READ | PROT_WRITE, 0,
      BCM2711_GPIO_BASE);
  set_pin_mode(NEXT_PIN, 0);
  set_pin_mode(PLAY_PIN, 0);
  set_pin_mode(PREV_PIN, 0);
  set_pin_pull(NEXT_PIN, PULL_UP);
  set_pin_pull(PLAY_PIN, PULL_UP);
  set_pin_pull(PREV_PIN, PULL_UP);

  while (1) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // std::cout << "next pin:" << read_pin(NEXT_PIN) << std::endl;
    if (!read_pin(NEXT_PIN)) {
      std::cout << "next" << std::endl;
      cd.next();
      while (!read_pin(NEXT_PIN))
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    } else if (!read_pin(PREV_PIN)) {
      std::cout << "prev" << read_pin(PREV_PIN) << std::endl;
      cd.prev();
      while (!read_pin(PREV_PIN))
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    } else if (!read_pin(PLAY_PIN)) {
      std::cout << "play" << std::endl;

      if (cd.get_state() == bambox::CdReader::State::PLAYING) {
        cd.pause();
      } else {
        cd.resume();
      }
      while (!read_pin(PLAY_PIN))
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }

  return 0;
}