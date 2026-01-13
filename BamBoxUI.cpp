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
#include "BamBoxUI.hpp"

#include <spdlog/spdlog.h>

#include <format>
#include <iomanip>

using bambox::BamBoxUI;

BamBoxUI::BamBoxUI() = default;
BamBoxUI::~BamBoxUI() = default;

bambox::Error BamBoxUI::init() { return {}; }

static const std::string s_style = R"(
window {
  background-color: #1f1f1f;
}

.big-button-icon {
  color: #EE44FF;
}


.big-button {
  font-size:28px;
  background-image: none;      /* REMOVE default background */
  box-shadow: none;            /* REMOVE theme shadows */
  background-color: white;
  border-radius:5px;
}

.button-hover {
  border-width: 5px;
  border-color: blue;
  border-style: solid;
  background-image: none;      /* REMOVE default background */
  box-shadow: none;            /* REMOVE theme shadows */
  background-color: gray;
}

.title {
  color: white;
  background-image: none;
  background-color: transparent;
  font-size:28px;
  margin-top: 40px;
}

progressbar.song-progress {
  margin-left : 10px;
  margin-right : 10px;
  margin-bottom: 20px;
}

progressbar.song-progress text {
    color: white;
    font-size:28px;
}

progress, trough {
  min-height: 13px;
}

)";

// thunk click for ui callbacks
namespace bambox {
class BamBoxUIThunker {
 public:
  static void activate(GtkApplication* app, gpointer user_data) { ((BamBoxUI*)user_data)->do_activate(); }
  static gboolean set_song_info(BamBoxUI* ui) {
    ui->do_set_song();
    return G_SOURCE_REMOVE;
  }
  static gboolean set_track_time(BamBoxUI* ui) {
    ui->do_set_track_time();
    return G_SOURCE_REMOVE;
  }

  static gboolean input_left(BamBoxUI* ui) {
    ui->do_input_left();
    return G_SOURCE_REMOVE;
  }

  static gboolean input_right(BamBoxUI* ui) {
    ui->do_input_right();
    return G_SOURCE_REMOVE;
  }

  static gboolean input_click(BamBoxUI* ui) {
    ui->do_input_click();
    return G_SOURCE_REMOVE;
  }
};
}  // namespace bambox

void BamBoxUI::set_song_info(const std::optional<CdReader::Song>& song) {
  spdlog::info("set_song_info()");
  song_ = song;
  g_idle_add(G_SOURCE_FUNC(&BamBoxUIThunker::set_song_info), this);
}

void BamBoxUI::set_set_track_time(const std::chrono::seconds sec) {
  current_time_ = sec;
  g_idle_add(G_SOURCE_FUNC(&BamBoxUIThunker::set_track_time), this);
}

void BamBoxUI::input_left() { g_idle_add(G_SOURCE_FUNC(&BamBoxUIThunker::input_left), this); }

void BamBoxUI::input_right() { g_idle_add(G_SOURCE_FUNC(&BamBoxUIThunker::input_right), this); }

void BamBoxUI::input_click() { g_idle_add(G_SOURCE_FUNC(&BamBoxUIThunker::input_click), this); }

void BamBoxUI::do_input_left() {
  if (selected_button_idx_ > 0) {
    gtk_widget_remove_css_class(GTK_WIDGET(buttons_[selected_button_idx_]), "button-hover");
    selected_button_idx_--;
    gtk_widget_add_css_class(GTK_WIDGET(buttons_[selected_button_idx_]), "button-hover");
  }
}

void BamBoxUI::do_input_right() {
  if (selected_button_idx_ < (buttons_.size() - 1)) {
    gtk_widget_remove_css_class(GTK_WIDGET(buttons_[selected_button_idx_]), "button-hover");
    selected_button_idx_++;
    gtk_widget_add_css_class(GTK_WIDGET(buttons_[selected_button_idx_]), "button-hover");
  }
}

void BamBoxUI::do_input_click() {}

void BamBoxUI::do_set_song() {
  spdlog::info("do_set_song()");

  std::string title = "";
  std::string artist = "";
  std::string album = "";

  if (cd_.has_value()) {
    artist = (!cd_->artist_.empty()) ? cd_->artist_ : "Unknown";
    album = (!cd_->title_.empty()) ? cd_->title_ : "Unknown";

    if (song_.has_value()) {
      title = (!song_->title_.empty()) ? song_->title_ : ("Track " + std::to_string(song_->track_num_));
      if (!song_->artist_.empty()) {
        artist = song_->artist_;
      }
    }
  } else {
    title = "No CD...";
  }

  gtk_label_set_text(title_text_, title.c_str());
  gtk_label_set_text(artist_text_, artist.c_str());
  gtk_label_set_text(album_text_, album.c_str());
}

void BamBoxUI::do_set_track_time() {
  double track_percent = 0;
  std::string time_text = "00:00/00:00";
  if (song_.has_value() && cd_.has_value()) {
    track_percent = static_cast<double>(current_time_.count()) / song_->length_.count();

    // Cries in GCC12 :( R.I.P std::format
    std::ostringstream ss;
    ss << std::setw(2) << std::setfill('0') << std::chrono::duration_cast<std::chrono::minutes>(current_time_).count()
       << ":" << std::setw(2) << std::setfill('0') << (current_time_.count() % 60) << "/" << std::setw(2)
       << std::setfill('0') << std::chrono::duration_cast<std::chrono::minutes>(song_->length_).count() << ":"
       << std::setw(2) << std::setfill('0') << (song_->length_.count() % 60);
    time_text = ss.str();
  }

  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(song_progress_), track_percent);
  gtk_progress_bar_set_text(GTK_PROGRESS_BAR(song_progress_), time_text.c_str());
}

void BamBoxUI::do_activate() {
  window_ = gtk_application_window_new(app_);
  gtk_window_set_title(GTK_WINDOW(window_), "Bam-Box");
  gtk_window_set_default_size(GTK_WINDOW(window_), 320, 240);
  gtk_window_fullscreen(GTK_WINDOW(window_));

  GtkCssProvider* provider = gtk_css_provider_new();
  GdkDisplay* display = gdk_display_get_default();
  gtk_css_provider_load_from_string(provider, s_style.c_str());
  gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  GtkIconTheme* icon_theme = gtk_icon_theme_get_for_display(display);
  gtk_icon_theme_add_search_path(icon_theme, "res");

  song_progress_ = gtk_progress_bar_new();
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(song_progress_), 0);
  gtk_progress_bar_set_text(GTK_PROGRESS_BAR(song_progress_), "00:00/00:00");
  gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(song_progress_), true);
  gtk_widget_add_css_class(song_progress_, "song-progress");
  gtk_widget_set_halign(song_progress_, GTK_ALIGN_FILL);
  gtk_widget_set_valign(song_progress_, GTK_ALIGN_END);
  gtk_widget_set_hexpand(song_progress_, FALSE);
  gtk_widget_set_vexpand(song_progress_, TRUE);

  auto* vlayout_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

  title_text_ = GTK_LABEL(gtk_label_new("Unknown"));
  gtk_widget_add_css_class(GTK_WIDGET(title_text_), "title");
  gtk_label_set_justify(title_text_, GTK_JUSTIFY_CENTER);
  gtk_widget_set_vexpand(GTK_WIDGET(title_text_), TRUE);
  gtk_label_set_wrap(title_text_, true);
  gtk_label_set_max_width_chars(title_text_, 32);

  artist_text_ = GTK_LABEL(gtk_label_new("Unknown"));
  gtk_widget_add_css_class(GTK_WIDGET(artist_text_), "title");
  gtk_label_set_justify(artist_text_, GTK_JUSTIFY_CENTER);
  gtk_widget_set_vexpand(GTK_WIDGET(artist_text_), TRUE);

  album_text_ = GTK_LABEL(gtk_label_new("Unknown"));
  gtk_widget_add_css_class(GTK_WIDGET(album_text_), "title");
  gtk_label_set_justify(album_text_, GTK_JUSTIFY_CENTER);
  gtk_widget_set_vexpand(GTK_WIDGET(album_text_), TRUE);

  auto* button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_halign(button_box, GTK_ALIGN_FILL);
  gtk_widget_set_valign(button_box, GTK_ALIGN_END);

  buttons_.push_back(GTK_BUTTON(gtk_button_new_from_icon_name("volume-symbolic")));
  buttons_.push_back(GTK_BUTTON(gtk_button_new_from_icon_name("headphone-symbolic")));
  buttons_.push_back(GTK_BUTTON(gtk_button_new_from_icon_name("song_list-symbolic")));
  buttons_.push_back(GTK_BUTTON(gtk_button_new_from_icon_name("settings-symbolic")));

  // Select the first button.
  gtk_widget_add_css_class(GTK_WIDGET(buttons_.front()), "button-hover");
  for (auto* button : buttons_) {
    gtk_widget_add_css_class(gtk_button_get_child(button), "big-button-icon");
    gtk_image_set_pixel_size(GTK_IMAGE(gtk_button_get_child(button)), 56);
    gtk_widget_add_css_class(GTK_WIDGET(button), "big-button");
    gtk_widget_set_hexpand(GTK_WIDGET(button), true);
    gtk_box_append(GTK_BOX(button_box), GTK_WIDGET(button));
  }

  gtk_box_append(GTK_BOX(vlayout_box), button_box);
  gtk_box_append(GTK_BOX(vlayout_box), GTK_WIDGET(title_text_));
  gtk_box_append(GTK_BOX(vlayout_box), GTK_WIDGET(artist_text_));
  gtk_box_append(GTK_BOX(vlayout_box), GTK_WIDGET(album_text_));
  gtk_box_append(GTK_BOX(vlayout_box), song_progress_);

  gtk_window_set_child(GTK_WINDOW(window_), vlayout_box);
  gtk_window_present(GTK_WINDOW(window_));
}

bambox::Error BamBoxUI::go() {
  app_ = gtk_application_new("ca.larrycloud.bambox", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app_, "activate", G_CALLBACK(&BamBoxUIThunker::activate), this);
  g_application_run(G_APPLICATION(app_), 0, nullptr);
  g_object_unref(G_APPLICATION(app_));

  return {};
}
