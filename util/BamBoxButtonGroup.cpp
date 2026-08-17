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
#include "util/BamBoxButtonGroup.hpp"

#include <algorithm>

using bambox::ui::BamBoxButtonGroup;

BamBoxButtonGroup::BamBoxButtonGroup() = default;

bambox::Error BamBoxButtonGroup::add_button(std::unique_ptr<BamBoxButton>&& button) {
  buttons_.push_back(std::move(button));
  return {};
}

void BamBoxButtonGroup::add_onhover(const HoverCb& hover) { hover_cb_ = hover; }

void BamBoxButtonGroup::next() {
  if (buttons_.empty()) {
    return;
  }
  // Clamped, not wrapped: the last button stays selected at the end of the group.
  select(std::min(static_cast<size_t>(idx_) + 1, buttons_.size() - 1));
}
void BamBoxButtonGroup::prev() {
  if (buttons_.empty()) {
    return;
  }
  select((idx_ > 0) ? static_cast<size_t>(idx_) - 1 : 0);
}
void BamBoxButtonGroup::click() {
  if (buttons_.empty()) {
    return;
  }
  buttons_[idx_]->activate();
}

void BamBoxButtonGroup::select(size_t idx) {
  if (idx >= buttons_.size()) {
    return;
  }

  if (static_cast<size_t>(idx_) < buttons_.size()) {
    buttons_[idx_]->set_active(false);
  }
  idx_ = static_cast<int>(idx);
  buttons_[idx_]->set_active(true);
  if (hover_cb_) hover_cb_(*buttons_[idx_].get(), idx_);
}
