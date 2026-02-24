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
#include <vector>

#include "util/BamBoxElement.hpp"
namespace bambox::ui {

class BamBoxButton : public BamBoxElement {
 public:
  using ClickCb = std::function<void(GtkButton* gtk_button, BamBoxButton* button)>;

 private:
  // Stored in class so pointer can be safely passed to static c function.
  std::vector<ClickCb> onclick_callbacks_{};

 public:
  BamBoxButton(GtkBuilder* builder, const char* path, const ClickCb& click);
  BamBoxButton(GtkButton* button, const ClickCb& click);
  BamBoxButton(const BamBoxButton&) = delete;
  BamBoxButton(BamBoxButton&&) = delete;
  BamBoxButton operator=(BamBoxButton&&) = delete;
  BamBoxButton operator=(const BamBoxButton&) = delete;

  void add_onclick(const ClickCb& click);
  void set_active(bool active);

  Error add_child_style(const char* cls);

  GtkButton* as_button() const;

};
}  // namespace bambox::ui
