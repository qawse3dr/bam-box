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

#include <memory>
#include <thread>

#include "AudioPlayer.hpp"
#include "BamBoxConfig.hpp"
#include "BamBoxError.hpp"
#include "CdReader.hpp"
#include "LcdDisplay.hpp"
#include "platform/Gpio.hpp"

#include <gtk/gtk.h>


namespace bambox {
class BamBox {
 private:
  enum class State { UNKNOWN, EJECTED, LOADING, PLAYING, NO_DISC, EXIT };
  enum class InputType { LEFT, RIGHT, PRESS, PREV, PLAY, NEXT };
  enum class InputState { MAIN, VOLUME, LIST};

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
  Error next();

 private:
  void cd_player_loop();

  // UI calls
  void ui_activate();
  void ui_update_track_info();
  void ui_update_album_art();
  void ui_update_track_time(const std::chrono::seconds sec);

  void ui_handle_input(InputType type);
  void ui_main_input(InputType type);
  void ui_volume_input(InputType type);
  void ui_list_input(InputType type);

 private:
  BamBoxConfig cfg_;

  // Cd functions
  std::unique_ptr<CdReader> cd_reader_{};
  std::unique_ptr<AudioPlayer> audio_player_{};
  std::shared_ptr<platform::Gpio> gpio_{};
  std::unique_ptr<LcdDisplay> lcd_display_{};

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
  GtkWidget* song_progress_{};
  GtkImage* album_art_{};

  GtkWindow* window_{};

  size_t selected_button_idx_ = 0;
  std::vector<GtkButton*> menu_buttons_{};
  std::vector<GtkButton*>* active_lists_{};

  // Stack and children screens
  GtkStack* screen_stack_{};
  GtkWidget* screen_stack_splash_{};
  GtkWidget* screen_stack_main_{};


  // Splash screen
  // Volume Overlay
  GtkWidget* volume_overlay_{};
  GtkProgressBar* volume_overlay_level_{};

  std::chrono::seconds current_time_{};
};
}  // namespace bambox