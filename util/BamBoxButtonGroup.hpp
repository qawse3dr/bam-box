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
#include <memory>
#include <vector>

#include "util/BamBoxButton.hpp"
namespace bambox::ui {

class BamBoxButtonGroup {
 public:
  using HoverCb = std::function<void(BamBoxButton& button, int position)>;

 private:
  // Stored in class so pointer can be safely passed to static c function.
  HoverCb hover_cb_{};

  std::vector<std::unique_ptr<BamBoxButton>> buttons_{};
  int idx_ = 0;

 public:
  BamBoxButtonGroup();
  BamBoxButtonGroup(const BamBoxButtonGroup&) = delete;
  BamBoxButtonGroup(BamBoxButtonGroup&&) = delete;
  BamBoxButtonGroup operator=(BamBoxButtonGroup&&) = delete;
  BamBoxButtonGroup operator=(const BamBoxButtonGroup&) = delete;

  Error add_button(std::unique_ptr<BamBoxButton>&& button);
  void add_onhover(const HoverCb& hover);

  void select(size_t idx);
  void next();
  void prev();
  void click();

  // Accessors
  inline int get_selected_idx() const { return idx_; }
  inline const std::unique_ptr<BamBoxButton>& back() const { return buttons_.back(); }
  inline const std::unique_ptr<BamBoxButton>& front() const { return buttons_.front(); }
  inline const std::unique_ptr<BamBoxButton>& selected() const { return buttons_[idx_]; }
  inline size_t size() { return buttons_.size(); }

  inline void clear() {
    buttons_.clear();
    idx_ = 0;
  }
};
}  // namespace bambox::ui
