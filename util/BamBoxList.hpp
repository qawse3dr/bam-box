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
#include "util/BamBoxButtonGroup.hpp"

namespace bambox::ui {
class BamBoxList : public BamBoxElement {
 public:
  using ClickCb = std::function<void(BamBoxButton& button, int idx)>;

 private:
  ClickCb cb_;
  BamBoxButtonGroup buttons_;
  GtkScrolledWindow* win_;

 public:
  /**
   * Create Gtk List wrapper.
   *
   * @param builder builder object which contains list
   * @param list_name name of the list object
   * @param window_name name of the window object if it exists, otherwise null
   * @param click OnClick listener
   */
  BamBoxList(GtkBuilder* builder, const char* list_name, const char* window_name, const ClickCb& click);

  /// Clears list
  void clear();

  // Add new entry with label
  void add_label(const char* label);

  void select(int idx);
  void prev();
  void next();
  void activate() override;
  size_t size();

  GtkListBox* as_list();
};
}  // namespace bambox::ui
