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

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <thread>

#include "BamBoxError.hpp"

namespace bambox::platform {

class Gpio {
 public:
  using GpioIRQ = std::function<void(unsigned int gpio, bool val)>;
  using GpioIRQHandle = uint64_t;
  enum class AltFunc { INPUT = 0, OUTPUT = 1, ALT_0 = 4, ALT_1 = 5, ALT_2 = 6, ALT_3 = 7, ALT_4 = 3, ALT_5 = 2 };

  enum class PullMode { NONE = 0, UP = 1, DOWN = 2 };

  // convert to bitset
  enum class TriggerType { RISING_EDGE, FALLING_EDGE, HIGH_LEVEL, LOW_LEVEL };

  // TODO common pins SPI I2c ...?
  enum class Board {

  };

 private:
  struct IRQInfo {
    GpioIRQ func;
    GpioIRQHandle handle;
    std::chrono::nanoseconds debounce;
    std::chrono::time_point<std::chrono::steady_clock> last_trigger{};
  };

 private:
  void gpio_ist_func();

 public:
  Gpio();
  ~Gpio();
  int init();

  void level_set(unsigned int gpio, bool high);

  /**
   * @brief Registers for an IRQ.
   *
   * @note no expensive operations should be done within the irq
   *       as it will block other waiting for value.
   */
  Expected<GpioIRQHandle> register_irq(unsigned int gpio, std::set<TriggerType> type, GpioIRQ irq_func,
                                       std::chrono::nanoseconds debounce_timeout = std::chrono::nanoseconds(0));
  int level_get(unsigned int gpio);

  void pull_mode_set(unsigned int gpio, PullMode mode);
  PullMode pull_mode_get(unsigned int gpio);

  void alt_func_set(unsigned int gpio, AltFunc alt_func);
  AltFunc alt_func_get(unsigned int gpio);

 private:
  volatile uint32_t *gpio_base_ = NULL;
  std::thread gpio_ist_thread_{};

  // map from gpio to assocated irq
  std::map<unsigned int, IRQInfo> irq_map_;
};
}  // namespace bambox::platform