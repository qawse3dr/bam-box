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

#include "util/BamBoxList.hpp"

#include <spdlog/spdlog.h>

using bambox::ui::BamBoxList;

BamBoxList::BamBoxList(GtkBuilder* builder, const char* list_name, const char* window_name, const ClickCb& click)
    : BamBoxElement(BamBoxElement::from_builder(builder, list_name)), cb_(click) {
  if (window_name) {
    win_ = GTK_SCROLLED_WINDOW(BamBoxElement::from_builder(builder, window_name));
    gtk_scrolled_window_set_policy(win_, GTK_POLICY_NEVER, GTK_POLICY_EXTERNAL);

    buttons_.add_onhover([&](BamBoxButton& button, int position) {
      auto row = gtk_list_box_get_row_at_index(as_list(), position);

      // Make sure the selected element is visible
      GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(win_);
      double current = gtk_adjustment_get_value(vadj);
      double page = gtk_adjustment_get_page_size(vadj);

      graphene_rect_t rect;
      (void)gtk_widget_compute_bounds(GTK_WIDGET(row), as_widget(), &rect);
      if (rect.origin.y < current) {
        gtk_adjustment_set_value(vadj, rect.origin.y);
      } else if (current + page < rect.origin.y + rect.size.height) {
        gtk_adjustment_set_value(vadj, rect.origin.y + rect.size.height - page);
      }
    });
  }
}

void BamBoxList::clear() {
  gtk_list_box_remove_all(as_list());
  buttons_.clear();
}

// Add new entry with label
void BamBoxList::add_label(const char* label) {
  buttons_.add_button(std::make_unique<BamBoxButton>(GTK_BUTTON(gtk_button_new_with_label(label)), nullptr));
  
  // TODO these shouldn't be hardcoded
  buttons_.back()->add_style("menu-button");
  buttons_.back()->add_child_style("overlay-list-text");
  auto* gtk_label = GTK_LABEL(gtk_button_get_child(buttons_.back()->as_button()));
  gtk_label_set_ellipsize(gtk_label, PANGO_ELLIPSIZE_END);
  gtk_label_set_max_width_chars(gtk_label, 15);
  
  gtk_list_box_append(as_list(), buttons_.back()->as_widget());
  select(0);
}

void BamBoxList::select(int idx) { buttons_.select(idx); }
void BamBoxList::prev() { buttons_.prev(); }
void BamBoxList::next() { buttons_.next(); }

void BamBoxList::activate() {
  spdlog::info("active list");
  if (cb_) {
    cb_(*buttons_.selected().get(), buttons_.get_selected_idx());
  }
}

size_t BamBoxList::size() { return buttons_.size(); }

GtkListBox* BamBoxList::as_list() { return GTK_LIST_BOX(as_widget()); }
