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

#include <screen/screen.h>

#include <thread>

#include "BamBoxError.hpp"
#include "platform/Gpio.hpp"

namespace bambox {

class LcdDisplay {
 public:
  enum class State { UNINIT, INIT, RUNNING, STOPPING };

 public:
  LcdDisplay(const std::shared_ptr<platform::Gpio>& gpio);
  ~LcdDisplay();

  Error init();
  Error go();

 private:
  void display_loop();
  void lcd_write_cmd(uint8_t data);
  void lcd_write_data(uint8_t data);
  void lcd_write_word(uint16_t data);

 private:
  std::thread display_thread_{};
  std::shared_ptr<platform::Gpio> gpio_;
  int spi_dev_ = -1;
  State state_ = State::UNINIT;

  screen_context_t screen_ctx_{};
  screen_display_t screen_dsy_{};
  screen_pixmap_t screen_pix_{};

  static constexpr int LCD_HEIGHT = 320;
  static constexpr int LCD_WIDTH = 240;

};
}  // namespace bambox