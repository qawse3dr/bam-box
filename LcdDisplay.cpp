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

#include "LcdDisplay.hpp"

#include <fcntl.h>
#include <hw/io-spi.h>
#include <spdlog/spdlog.h>
#include <sys/mman.h>
#include <unistd.h>

#define LCD_RST 27
#define LCD_DC 25
#define LCD_BL 13

using bambox::LcdDisplay;
using bambox::platform::Gpio;

LcdDisplay::LcdDisplay(const std::shared_ptr<platform::Gpio> &gpio) : gpio_(gpio) {}

LcdDisplay::~LcdDisplay() {
  if (state_ == State::RUNNING) {
    state_ = State::STOPPING;
    display_thread_.join();
  }
  if (spi_dev_ == -1) {
    close(spi_dev_);
  }

  screen_destroy_pixmap(screen_pix_);
  screen_destroy_context(screen_ctx_);
}

bambox::Error LcdDisplay::init() {
  if (state_ != State::UNINIT) {
    return {ECode::ERR_INVAL_STATE, "Already Initalized"};
  }
  gpio_->alt_func_set(LCD_RST, Gpio::AltFunc::OUTPUT);
  gpio_->alt_func_set(LCD_DC, Gpio::AltFunc::OUTPUT);
  gpio_->alt_func_set(LCD_BL, Gpio::AltFunc::OUTPUT);
  gpio_->level_set(LCD_BL, true);

  spi_dev_ = open("/dev/io-spi/spi0/dev0", O_RDWR);
  if (spi_dev_ == -1) {
    return {ECode::ERR_NOFILE, "Failed to open SPI Device"};
  }

  spi_cfg_t spi_cfg = {.mode = SPI_MODE_WORD_WIDTH_32 | SPI_MODE_CPHA_0 | SPI_MODE_CPOL_0, .clock_rate = 25000000};
  if (devctl(spi_dev_, DCMD_SPI_SET_CONFIG, &spi_cfg, sizeof(spi_cfg), NULL) < 0) {
    return {ECode::ERR_IO, "Failed configure Spi Dev"};
  }

  // Reset the display
  usleep(100);
  gpio_->level_set(LCD_RST, false);
  usleep(100);
  gpio_->level_set(LCD_RST, true);
  usleep(100);

  // Write configuration

#define MADCTL_ROW_COLUMN_EXCHANGE (1 << 5)
#define MADCTL_BGR_PIXEL_ORDER (1 << 3)
#define MADCTL_COLUMN_ADDRESS_ORDER_SWAP (1 << 6)
#define MADCTL_ROW_ADDRESS_ORDER_SWAP (1 << 7)
#define MADCTL_ROTATE_180_DEGREES (MADCTL_COLUMN_ADDRESS_ORDER_SWAP | MADCTL_ROW_ADDRESS_ORDER_SWAP)
  uint8_t madctl = 0;

  lcd_write_cmd(0x36);
  lcd_write_data(madctl);

  // format
  lcd_write_cmd(0x3A);
  lcd_write_data(0x05);

  lcd_write_cmd(0x21);

  lcd_write_cmd(0x2A);
  lcd_write_data(0x00);
  lcd_write_data(0x00);
  lcd_write_data(0x01);
  lcd_write_data(0x3F);

  lcd_write_cmd(0x2B);
  lcd_write_data(0x00);
  lcd_write_data(0x00);
  lcd_write_data(0x00);
  lcd_write_data(0xEF);

  lcd_write_cmd(0xB2);
  lcd_write_data(0x0C);
  lcd_write_data(0x0C);
  lcd_write_data(0x00);
  lcd_write_data(0x33);
  lcd_write_data(0x33);

  lcd_write_cmd(0xB7);
  lcd_write_data(0x35);

  lcd_write_cmd(0xBB);
  lcd_write_data(0x1F);

  lcd_write_cmd(0xC0);
  lcd_write_data(0x2C);

  lcd_write_cmd(0xC2);
  lcd_write_data(0x01);

  lcd_write_cmd(0xC3);
  lcd_write_data(0x12);

  lcd_write_cmd(0xC4);
  lcd_write_data(0x20);

  lcd_write_cmd(0xC6);
  lcd_write_data(0x0F);

  lcd_write_cmd(0xD0);
  lcd_write_data(0xA4);
  lcd_write_data(0xA1);

  lcd_write_cmd(0xE0);
  lcd_write_data(0xD0);
  lcd_write_data(0x08);
  lcd_write_data(0x11);
  lcd_write_data(0x08);
  lcd_write_data(0x0C);
  lcd_write_data(0x15);
  lcd_write_data(0x39);
  lcd_write_data(0x33);
  lcd_write_data(0x50);
  lcd_write_data(0x36);
  lcd_write_data(0x13);
  lcd_write_data(0x14);
  lcd_write_data(0x29);
  lcd_write_data(0x2D);

  lcd_write_cmd(0xE1);
  lcd_write_data(0xD0);
  lcd_write_data(0x08);
  lcd_write_data(0x10);
  lcd_write_data(0x08);
  lcd_write_data(0x06);
  lcd_write_data(0x06);
  lcd_write_data(0x39);
  lcd_write_data(0x44);
  lcd_write_data(0x51);
  lcd_write_data(0x0B);
  lcd_write_data(0x16);
  lcd_write_data(0x14);
  lcd_write_data(0x2F);
  lcd_write_data(0x31);
  lcd_write_cmd(0x21);

  lcd_write_cmd(0x11);

  lcd_write_cmd(0x29);

  if (screen_create_context(&screen_ctx_, SCREEN_DISPLAY_MANAGER_CONTEXT) != 0) {
    return {ECode::ERR_IO, "Failed to create screen ctx"};
  }

  int ndisplays = 0;
  screen_get_context_property_iv(screen_ctx_, SCREEN_PROPERTY_DISPLAY_COUNT, &ndisplays);

  screen_display_t *screen_dlist = (screen_display_t *)calloc(ndisplays, sizeof(*screen_dlist));
  screen_get_context_property_pv(screen_ctx_, SCREEN_PROPERTY_DISPLAYS, (void **)screen_dlist);

  screen_dsy_ = screen_dlist[0];  // any screen_display_t from screen_dlist

  free(screen_dlist);

  printf("Got %d displays\n", ndisplays);

  screen_create_pixmap(&screen_pix_, screen_ctx_);

  int size[2] = {320, 240};
  int usage = SCREEN_USAGE_READ | SCREEN_USAGE_NATIVE;
  if (screen_set_pixmap_property_iv(screen_pix_, SCREEN_PROPERTY_USAGE, &usage) != 0) {
    return {ECode::ERR_UNKNOWN, "Failed to set screen_pix usage"};
  }

  int format = SCREEN_FORMAT_RGB565;
  screen_set_pixmap_property_iv(screen_pix_, SCREEN_PROPERTY_FORMAT, &format);
  screen_set_pixmap_property_iv(screen_pix_, SCREEN_PROPERTY_BUFFER_SIZE, size);

  state_ = State::INIT;
  return {};
}

bambox::Error LcdDisplay::go() {
  if (state_ != State::INIT) {
    return {ECode::ERR_INVAL_STATE, "Not Initialized"};
  }

  state_ = State::RUNNING;
  display_thread_ = std::thread(&LcdDisplay::display_loop, this);

  return {};
}

uint16_t swap16(uint16_t v) { return (v >> 8) | (v << 8); }
void convert_img(uint16_t screen_buf[320][240], uint16_t *src) {
  const int src_w = 320;
  const int src_h = 240;

  for (int dy = 0; dy < 320; dy++) {
    for (int dx = 0; dx < 240; dx++) {
      // Rotates screen 270 degrees
      int sx = src_w - 1 - dy;
      int sy = dx;
      if (sx >= src_w || sy >= src_h) {
        // This should be impossible but check to be safe
        continue;
      }

      // The image seems to be in the wrong endian ness most likely due to the spi configuration
      // so swap them to get the right colours
      screen_buf[dy][dx] = swap16(src[sy * src_w + sx]);
    }
  }
}

void LcdDisplay::display_loop() {
  screen_buffer_t screen_pix_buf;
  uint16_t *screen_pix_ptr = NULL;
  uint16_t screen_buf[LCD_HEIGHT][LCD_WIDTH];

  screen_create_pixmap_buffer(screen_pix_);
  screen_get_pixmap_property_pv(screen_pix_, SCREEN_PROPERTY_RENDER_BUFFERS, (void **)&screen_pix_buf);
  screen_get_buffer_property_pv(screen_pix_buf, SCREEN_PROPERTY_POINTER, (void **)&screen_pix_ptr);

  // SET windows
  lcd_write_cmd(0x2a);
  lcd_write_data(0);
  lcd_write_data(0);
  lcd_write_data((240 - 1) >> 8);
  lcd_write_data((240 - 1) & 0xff);

  lcd_write_cmd(0x2b);
  lcd_write_data(0);
  lcd_write_data(0);
  lcd_write_data((320 - 1) >> 8);
  lcd_write_data((320 - 1) & 0xff);
  lcd_write_cmd(0x2C);
  // Img loop
  while (1) {
    // Read and convert the image
    screen_read_display(screen_dsy_, screen_pix_buf, 0, NULL, 0);
    convert_img(screen_buf, screen_pix_ptr);

    // Write screen buffer to display.
    gpio_->level_set(LCD_DC, true);
    for (int i = 0; i < LCD_HEIGHT; i++) {
      write(spi_dev_, screen_buf[i], LCD_WIDTH * 2);
    }
    gpio_->level_set(LCD_DC, false);

    usleep(33330);  // roughly 30 fps.
  }
}

void LcdDisplay::lcd_write_cmd(uint8_t data) {
  gpio_->level_set(LCD_DC, false);
  write(spi_dev_, &data, sizeof(data));
}

void LcdDisplay::lcd_write_data(uint8_t data) {
  gpio_->level_set(LCD_DC, true);
  write(spi_dev_, &data, sizeof(data));
}

void LcdDisplay::lcd_write_word(uint16_t data) {
  gpio_->level_set(LCD_DC, true);
  write(spi_dev_, &data, sizeof(data));
}
