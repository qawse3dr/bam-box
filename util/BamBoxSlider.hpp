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

#include <functional>

#include "util/BamBoxElement.hpp"

namespace bambox::ui {

class BamBoxSlider : public BamBoxElement {
 public:
  using OnChangeCB = std::function<void(int val)>;
  using OnCommitCB = std::function<void(int val)>;
  using TextGenCB = std::function<std::string(int val)>;


 private:
  OnChangeCB change_cb_{};
  OnCommitCB commit_cb_{};
  TextGenCB text_gen_cb_{};

  int value_ = 0;

 public:
  BamBoxSlider(GtkBuilder* builder, const char* path, OnChangeCB change_cb, OnCommitCB commit_cb, TextGenCB text_gen_cb);
  BamBoxSlider(const BamBoxSlider&) = delete;
  BamBoxSlider(BamBoxSlider&&) = delete;
  BamBoxSlider operator=(BamBoxSlider&&) = delete;
  BamBoxSlider operator=(const BamBoxSlider&) = delete;

  void init(int value);
  void init_async(int value); // Init but called in UI thread.
  void update(int delta);
  void commit();
  inline int value() { return value_; }

  inline GtkProgressBar* as_progress_bar() { return GTK_PROGRESS_BAR(as_widget()); }

};
}  // namespace bambox::ui
