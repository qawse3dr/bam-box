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

/**
 * Based on https://datasheets.raspberrypi.com/bcm2711/bcm2711-peripherals.pdf
 * chatper 5
 */
#include "platform/Gpio.hpp"

#include <sys/mman.h>
#include <sys/neutrino.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <iostream>

#define BCM2711_GPIO_BASE 0xfe200000
#define BLOCK_SIZE (4 * 1024)

#define GPIO_SET 7
#define GPIO_CLR 10
#define GPIO_READ 13
#define GPIO_PULL_SET 57
#define GPIO_PULL_NONE 0
#define PULL_DOWN 2
#define PULL_UP 1

#define GPIO_GPEDS0 0x40 / 4  // GPIO event detection register
#define GPIO_GPREN0 0x4c / 4  // GPIO rising edge detection enable
#define GPIO_GPFEN0 0x58 / 4  // GPIO falling edge detection enable
#define GPIO_GPHEN0 0x64 / 4  // GPIO high level detection enable
#define GPIO_GPLEN0 0x70 / 4  // GPIO low level detection enable

#define GPIO_BANK0_IRQ 145

using bambox::platform::Gpio;

Gpio::Gpio() = default;
Gpio::~Gpio() {
  munmap_device_memory((void *)gpio_base_, BLOCK_SIZE);
  // TODO(qawse3dr) cancel ist thread
}

int Gpio::init() {
  gpio_base_ = (volatile uint32_t *)mmap_device_memory(NULL, BLOCK_SIZE, PROT_NOCACHE | PROT_READ | PROT_WRITE, 0,
                                                       BCM2711_GPIO_BASE);
  if (gpio_base_ == MAP_FAILED) {
    return errno;
  }

  gpio_ist_thread_ = std::thread(&Gpio::gpio_ist_func, this);
  return EOK;
}

void Gpio::level_set(unsigned int gpio, bool high) {
  auto *level_ptr = gpio_base_ + ((high) ? GPIO_SET : GPIO_CLR);
  *level_ptr = (0x1u << gpio);
}
bambox::Expected<Gpio::GpioIRQHandle> Gpio::register_irq(unsigned int gpio, std::set<TriggerType> type,
                                                         GpioIRQ irq_func, std::chrono::nanoseconds debounce_timeout) {
  // Disable all of them
  *(gpio_base_ + GPIO_GPREN0) &= ~(1 << gpio);
  *(gpio_base_ + GPIO_GPFEN0) &= ~(1 << gpio);
  *(gpio_base_ + GPIO_GPHEN0) &= ~(1 << gpio);
  *(gpio_base_ + GPIO_GPLEN0) &= ~(1 << gpio);
  *(gpio_base_ + GPIO_GPEDS0) = (1 << gpio);  // Clear the event

  if (type.count(TriggerType::RISING_EDGE)) {
    *(gpio_base_ + GPIO_GPREN0) |= (1 << gpio);
  }

  if (type.count(TriggerType::FALLING_EDGE)) {
    *(gpio_base_ + GPIO_GPFEN0) |= (1 << gpio);
  }

  if (type.count(TriggerType::HIGH_LEVEL)) {
    *(gpio_base_ + GPIO_GPHEN0) |= (1 << gpio);
  }

  if (type.count(TriggerType::LOW_LEVEL)) {
    *(gpio_base_ + GPIO_GPLEN0) |= (1 << gpio);
  }

  static GpioIRQHandle handle = 0;
  irq_map_.insert(std::pair<unsigned int, IRQInfo>(gpio, (IRQInfo){irq_func, handle++, debounce_timeout}));
  return {1};
}

int Gpio::level_get(unsigned int gpio) { return (*(gpio_base_ + GPIO_READ) >> gpio) & 0x1; }

void Gpio::pull_mode_set(unsigned int gpio, PullMode mode) {
  uint32_t pullreg = (GPIO_PULL_SET + (gpio >> 4));
  const uint32_t pullshift = (uint32_t)((gpio & 0xf) << 1);
  unsigned int pullbits = *(gpio_base_ + pullreg);
  pullbits &= ~(3 << pullshift);
  pullbits |= (static_cast<int>(mode) << pullshift);
  *(gpio_base_ + pullreg) = pullbits;
}

Gpio::PullMode Gpio::pull_mode_get(unsigned int gpio) {
  const uint32_t pull_reg = *(gpio_base_ + GPIO_PULL_SET + (gpio >> 4));
  PullMode mode = static_cast<PullMode>((pull_reg >> ((gpio & 0xf) << 1)) & 0x3);
  return mode;
}

void Gpio::alt_func_set(unsigned int gpio, AltFunc alt_func) {
  uint32_t reg = gpio / 10;
  const uint32_t sel = gpio % 10;
  uint32_t val;
  val = *(gpio_base_ + reg);
  val &= ~(0x7 << (3 * sel));
  val |= (static_cast<int>(alt_func) & 0x7) << (3 * sel);
  *(gpio_base_ + reg) = val;
}

Gpio::AltFunc Gpio::alt_func_get(unsigned int gpio) {
  const uint32_t reg = gpio / 10;
  const uint32_t sel = gpio % 10;
  return static_cast<AltFunc>(((*(gpio_base_ + reg)) >> (3 * sel)) & 0x7);
}

void Gpio::gpio_ist_func() {
  struct sched_param sp;
  sp.sched_priority = 1;  // real-time priority, >0
  pthread_setschedparam(pthread_self(), SCHED_RR, &sp);

  // Attach this thread to the interrupt source.
  int id = InterruptAttachThread(GPIO_BANK0_IRQ, _NTO_INTR_FLAGS_NO_UNMASK);
  if (id == -1) {
    std::cout << "failed to attach to thread" << std::endl;
    return;
  }

  while (1) {
    int rc = InterruptWait(_NTO_INTR_WAIT_FLAGS_UNMASK | _NTO_INTR_WAIT_FLAGS_FAST, NULL);
    if (rc != 0) {
      std::cout << "failed to wait for interupt" << std::endl;
      continue;
    }

    auto now = std::chrono::steady_clock::now();

    uint32_t events = *(gpio_base_ + GPIO_GPEDS0);
    for (auto &[gpio, irq_info] : irq_map_) {
      if (events & (1U << gpio)) {
        if ((irq_info.last_trigger + irq_info.debounce) < now) {
          irq_info.func(gpio, level_get(gpio));
        }
        irq_info.last_trigger = now;
        *(gpio_base_ + GPIO_GPEDS0) |= (1U << gpio);
      }
    }
    InterruptUnmask(0, id);
  }
  // Handle errors.
  InterruptDetach(id);
}
