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

#include <gtk/gtk.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <stack>
#include <thread>

#include "AudioPlayer.hpp"
#include "BamBoxConfig.hpp"
#include "BamBoxError.hpp"
#include "CdReader.hpp"
#include "LcdDisplay.hpp"
#include "platform/Gpio.hpp"

namespace bambox {
class BamBox {
 private:
  enum class State { UNKNOWN, EJECTED, LOADING, PLAYING, NO_DISC, EXIT };
  enum class InputType { LEFT, RIGHT, PRESS, PREV, PLAY, NEXT };
  enum class InputState { MAIN, VOLUME, LIST, SETTINGS, INFO };

  using UIStackPopFunc = std::function<void(void)>;
  using UIStackElement = std::pair<InputState, UIStackPopFunc>;

 public:
  BamBox();
  virtual ~BamBox();
  BamBox(BamBox&&) = delete;
  BamBox(const BamBox&) = delete;

  // Doesn't return
  Error go();
  Error config(BamBoxConfig&& cfg);
  void stop();

  // Music controls
  Error pause();
  Error resume();  // todo rename to play()?
  Error prev();

  /**
   * @brief Plays next track, or given track
   *
   * @param track Track to play, if negative one will default to next track
   */
  Error next(int track = -1);

 private:
  void cd_player_loop();

  // UI calls
  void ui_activate();
  void ui_update_track_info();
  void ui_update_album_art();
  void ui_update_track_time(const std::chrono::seconds sec);

  void ui_hide_overlay() {
    if (!active_overlay_) {
      return;
    }

    gtk_widget_set_visible(active_overlay_, false);
    active_overlay_ = nullptr;
  }

  void ui_push_stack(InputState new_state, const UIStackPopFunc& pop_func) {
    spdlog::info("Changing to screen {}", static_cast<int>(new_state));
    ui_stack_.emplace(input_state_, pop_func);
    input_state_ = new_state;
  }

  void ui_pop_stack() {
    if (ui_stack_.empty()) {
      spdlog::warn("Tried to pop state but no elements are on the stack");
      return;
    }

    auto element = ui_stack_.top();
    spdlog::info("Pop screen from {} to {}", static_cast<int>(input_state_), static_cast<int>(element.first));
    ui_stack_.pop();
    element.second();
    input_state_ = element.first;
  }

  void ui_show_overlay(GtkWidget* overlay, InputState state) {
    if (overlay == nullptr) {
      return;
    }
    active_overlay_ = overlay;
    gtk_widget_set_visible(active_overlay_, true);
    gtk_widget_set_opacity(active_overlay_, 1.0);
    ui_push_stack(state, std::bind(&BamBox::ui_hide_overlay, this));
  }

  void ui_set_button_active(GtkButton* button, bool active) {
    if (active) {
      gtk_widget_set_state_flags(GTK_WIDGET(button), GTK_STATE_FLAG_PRELIGHT, false);
    } else {
      gtk_widget_unset_state_flags(GTK_WIDGET(button), GTK_STATE_FLAG_PRELIGHT);
    }
  }

  void ui_set_list(GtkListBox* list, size_t length, GtkScrolledWindow* window, size_t selected = 0) {
    assert(list == nullptr && "list null");

    active_lists_idx_ = selected;
    active_list_ = list;
    active_lists_win_ = window;
    active_list_len_ = length;

    // Select the correct idx.
    auto row = gtk_list_box_get_row_at_index(active_list_, active_lists_idx_);
    gtk_widget_set_state_flags(gtk_list_box_row_get_child(row), GTK_STATE_FLAG_PRELIGHT, false);

    if (window != nullptr) {
      gtk_scrolled_window_set_policy(window, GTK_POLICY_NEVER, GTK_POLICY_EXTERNAL);
    }
  }

  void ui_handle_input(InputType type);
  void ui_main_input(InputType type);
  void ui_volume_input(InputType type);
  void ui_list_input(InputType type);
  void ui_setting_input(InputType type);
  void ui_info_input(InputType type);

  

 private:
  BamBoxConfig cfg_;

  // Cd functions
  std::unique_ptr<CdReader> cd_reader_{};
  std::unique_ptr<AudioPlayer> audio_player_{};
  std::shared_ptr<platform::Gpio> gpio_{};
  std::unique_ptr<LcdDisplay> lcd_display_{};

  std::stack<UIStackElement> ui_stack_{};

  // Running state
  std::thread cd_thread_{};
  std::mutex mtx_{};
  std::condition_variable cv_{};
  State state_ = State::UNKNOWN;
  bool is_paused_ = false;
  bool seek_request_ = false;

  // UI application
  GtkApplication* app_{};
  InputState input_state_ = InputState::MAIN;

  // Main screen song info.
  // [track, album, artist]
  std::array<GtkLabel*, 3> song_info_text_;
  GtkProgressBar* song_progress_{};
  GtkImage* album_art_{};

  GtkWindow* window_{};

  size_t menu_button_idx_ = 0;
  std::vector<GtkButton*> menu_buttons_{};

  size_t setting_button_idx_ = 0;
  std::vector<GtkButton*> setting_buttons_{};

  // List menu
  GtkListBox* active_list_{};
  GtkScrolledWindow* active_lists_win_{};
  GtkWidget* active_overlay_{};
  int active_lists_idx_ = 0;
  size_t active_list_len_ = 0;

  // Stack and children screens
  GtkStack* screen_stack_{};

  // Volume Overlay
  GtkWidget* volume_overlay_{};
  GtkProgressBar* volume_overlay_level_{};

  // Audio Select Overlay
  GtkWidget* output_overlay_{};
  GtkListBox* output_overlay_list_{};

  // Tracks Select Overlay
  GtkWidget* tracks_overlay_{};
  GtkListBox* tracks_overlay_list_{};
  GtkScrolledWindow* tracks_overlay_win_{};

  // Setting Overlays
  GtkWidget* settings_about_overlay_{};
  GtkWidget* settings_dump_overlay_{};


  std::chrono::seconds current_time_{};
};
}  // namespace bambox