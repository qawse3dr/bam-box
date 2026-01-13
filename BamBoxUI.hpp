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

#include <gtk/gtk.h>

#include <optional>
#include <thread>

#include "BamBoxError.hpp"
#include "CdReader.hpp"

namespace bambox {

class BamBoxUIThunker;

class BamBoxUI {
 public:
  enum class InputType { LEFT, RIGHT, PRESS };

 public:
  BamBoxUI();
  ~BamBoxUI();

  Error init();

  // Must be called on main thread
  Error go();

  void set_song_info(const std::optional<CdReader::Song>& song);
  void set_set_track_time(const std::chrono::seconds sec);
  void set_cd_info(std::optional<CdReader::CD> cd) {
    cd_ = cd;
    set_song_info({});
  }

  // external UI controls for rotary encoder input.
  void input_left();
  void input_right();
  void input_click();

 private:
  friend class BamBoxUIThunker;
  // Internal Handlers ()
  void do_set_song();
  void do_activate();
  void do_set_track_time();
  void do_input_left();
  void do_input_right();
  void do_input_click();

 private:
  GtkWidget* window_{};
  GtkApplication* app_{};
  GtkLabel* title_text_{};
  GtkLabel* artist_text_{};
  GtkLabel* album_text_{};
  GtkWidget* song_progress_{};

  int selected_button_idx_ = 0;
  std::vector<GtkButton*> buttons_{};

  // TODO create abstraction layer for currently selected element which has 3 inputs (left right click)
  // The selected element can be menu or something like a volume control widget

  std::optional<CdReader::Song> song_{};
  std::optional<CdReader::CD> cd_{};
  std::chrono::seconds current_time_{};
};
}  // namespace bambox