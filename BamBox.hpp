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
#include "CdPlayer.hpp"
#include "CdReader.hpp"
#include "LcdDisplay.hpp"
#include "platform/Gpio.hpp"
#include "util/BamBoxButtonGroup.hpp"
#include "util/BamBoxList.hpp"
#include "util/BamBoxSlider.hpp"

namespace bambox {
class BamBox {
 private:
  enum class InputType { LEFT, RIGHT, PRESS, PREV, PLAY, NEXT };
  enum class InputState { BUTTON_GROUP, VOLUME, LIST, INFO };

  // TODO make this also contain the active element.
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

  /**
   * @brief Plays next track, or given track
   *
   * @param track Track to play, if negative one will default to next track
   */
  Error next(int track = -1);
  Error prev();

 private:

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

  void ui_set_list(std::shared_ptr<ui::BamBoxList>& list, size_t selected = 0) {
    assert(list == nullptr && "list null");

    active_list_ = list;
    active_list_->select(selected);
  }

  void ui_handle_input(InputType type);
  void ui_button_group_input(InputType type);
  void ui_volume_input(InputType type);
  void ui_list_input(InputType type);
  void ui_info_input(InputType type);

  void cd_player_event(CdPlayer::Event e, const CdPlayer::EventData& data);

 private:
  BamBoxConfig cfg_;

  // Cd functions
  std::shared_ptr<CdReader> cd_reader_{};
  std::shared_ptr<AudioPlayer> audio_player_{};
  std::unique_ptr<CdPlayer> cd_player_{};
  std::shared_ptr<platform::Gpio> gpio_{};
  std::unique_ptr<LcdDisplay> lcd_display_{};

  std::stack<UIStackElement> ui_stack_{};

  // UI application
  GtkApplication* app_{};
  GtkWindow* window_{};
  InputState input_state_ = InputState::BUTTON_GROUP;

  // Main screen song info.
  // [track, album, artist]
  ui::BamBoxButtonGroup menu_buttons_{};
  std::array<GtkLabel*, 3> song_info_text_;
  std::shared_ptr<ui::BamBoxSlider> song_progress_{};
  GtkImage* album_art_{};

  // Active objects
  std::shared_ptr<ui::BamBoxList> active_list_{};
  GtkWidget* active_overlay_{};
  std::shared_ptr<ui::BamBoxSlider> active_slider_{};

  // Stack and children screens
  GtkStack* screen_stack_{};

  // Volume Overlay
  GtkWidget* volume_overlay_{};
  std::shared_ptr<ui::BamBoxSlider> volume_overlay_level_{};

  // Audio Select Overlay
  std::shared_ptr<ui::BamBoxList> output_overlay_list_{};
  GtkWidget* output_overlay_{};

  // Tracks Select Overlay
  GtkWidget* tracks_overlay_{};
  std::shared_ptr<ui::BamBoxList> tracks_overlay_list_{};

  // Setting Page
  ui::BamBoxButtonGroup setting_buttons_{};
  GtkScrolledWindow* setting_win_{};
  GtkLabel* settting_output_label_{};
  GtkLabel* settting_volume_label_{};
  GtkSwitch* settting_theme_switch_{};
  GtkLabel* settting_dump_label_{};

  ui::BamBoxButtonGroup* selected_button_ = &menu_buttons_;

  // Setting Overlays
  GtkWidget* settings_about_overlay_{};

  GtkWidget* settings_dump_overlay_{};
  ui::BamBoxButtonGroup dump_buttons_{};


  GtkWidget* settings_volume_overlay_{};
  std::shared_ptr<ui::BamBoxSlider> settings_volume_slider_{};

  GtkWidget* settings_output_overlay_{};
  std::shared_ptr<ui::BamBoxList> settings_output_overlay_list_{};

  GtkWidget* settings_cd_info_overlay_{};
  GtkLabel* cd_info_album_name_{};
  GtkLabel* cd_info_artist_name_{};
  GtkLabel* cd_info_track_count_{};
  GtkLabel* cd_info_album_length_{};
  GtkLabel* cd_info_release_date_{};
  GtkImage* cd_info_album_art_{};

  std::chrono::seconds current_time_{};
  CdReader::CD current_cd_{};
  CdReader::Song current_song_{};
};
}  // namespace bambox
