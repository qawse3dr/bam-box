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
#include "util/BamBoxElement.hpp"

#include <spdlog/spdlog.h>

#include <cassert>
#include <cstdlib>

using bambox::ui::BamBoxElement;

BamBoxElement::BamBoxElement(GtkWidget* widget) : widget_(widget) {}

GtkWidget* BamBoxElement::as_widget() const { return widget_; }

GtkWidget* BamBoxElement::from_builder(GtkBuilder* builder, const char* name) {
  auto* obj = gtk_builder_get_object(builder, name);
  if (obj == nullptr) {
    spdlog::critical("Failed to find element {}", name);
    exit(1);
  }
  return GTK_WIDGET(obj);
}

bambox::Error BamBoxElement::add_style(const char* cls) {
  assert(widget_ != nullptr);
  gtk_widget_add_css_class(widget_, cls);
  return {};
}

void BamBoxElement::activate() { gtk_widget_activate(as_widget()); }
