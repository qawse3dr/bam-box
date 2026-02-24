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
#include "util/BamBoxButton.hpp"

using bambox::ui::BamBoxButton;

BamBoxButton::BamBoxButton(GtkBuilder* builder, const char* path, const ClickCb& cb)
    : BamBoxButton(GTK_BUTTON(BamBoxElement::from_builder(builder, path)), cb) {}

BamBoxButton::BamBoxButton(GtkButton* button, const ClickCb& cb) : BamBoxElement(GTK_WIDGET(button)) {
  if (cb) {
    add_onclick(cb);
  }
}

void BamBoxButton::add_onclick(const ClickCb& click) {
  onclick_callbacks_.push_back(click);

  // Only register on first callback to avoid multiple triggers.
  if (onclick_callbacks_.size() == 1) {
    g_signal_connect(as_button(), "clicked", G_CALLBACK(+[](GtkButton* gtk_button, BamBoxButton* button) -> void {
                       for (const auto& cb : button->onclick_callbacks_) {
                         cb(gtk_button, button);
                       }
                     }),
                     this);
  }
}

GtkButton* BamBoxButton::as_button() const { return GTK_BUTTON(as_widget()); }

void BamBoxButton::set_active(bool active) {
  if (active) {
    gtk_widget_set_state_flags(as_widget(), GTK_STATE_FLAG_PRELIGHT, false);
  } else {
    gtk_widget_unset_state_flags(as_widget(), GTK_STATE_FLAG_PRELIGHT);
  }
}

bambox::Error BamBoxButton::add_child_style(const char* cls) {
  gtk_widget_add_css_class(gtk_button_get_child(as_button()), cls);
  return {};
}
