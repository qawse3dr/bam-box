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

#include "util/BamBoxSlider.hpp"

using bambox::ui::BamBoxSlider;

BamBoxSlider::BamBoxSlider(GtkBuilder* builder, const char* path, OnChangeCB change_cb, OnCommitCB commit_cb,
                           TextGenCB text_gen_cb)
    : BamBoxElement(BamBoxElement::from_builder(builder, path)),
      change_cb_(change_cb),
      commit_cb_(commit_cb),
      text_gen_cb_(text_gen_cb) {}

void BamBoxSlider::init(int value) {
  value_ = value;
  gtk_progress_bar_set_fraction(as_progress_bar(), value_ / 100.0);
  if (text_gen_cb_) {
    gtk_progress_bar_set_text(as_progress_bar(), text_gen_cb_(value_).c_str());
  }
  if (change_cb_) {
    change_cb_(value_);
  }
}

void BamBoxSlider::init_async(int value) {
  value_ = value;
  auto cb = (GSourceOnceFunc) + [](BamBoxSlider* slider) {
    gtk_progress_bar_set_fraction(slider->as_progress_bar(), slider->value_ / 100.0);
    if (slider->text_gen_cb_) {
      gtk_progress_bar_set_text(slider->as_progress_bar(), slider->text_gen_cb_(slider->value_).c_str());
    }
  };
  g_idle_add_once(cb, this);
}

void BamBoxSlider::update(int delta) {
  value_ += delta;
  gtk_progress_bar_set_fraction(as_progress_bar(), value_ / 100.0);
  if (text_gen_cb_) {
    gtk_progress_bar_set_text(as_progress_bar(), text_gen_cb_(value_).c_str());
  }

  if (change_cb_) {
    change_cb_(value_);
  }
}

void BamBoxSlider::commit() {
  if (commit_cb_) {
    commit_cb_(value_);
  }
}
