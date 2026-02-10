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

#include "BamBox.hpp"

#include <spdlog/spdlog.h>
#include <sys/dcmd_cam.h>

#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>

#include "AudioPlayer.hpp"
#include "BamBoxError.hpp"
#include "CdReader.hpp"
#include "platform/Gpio.hpp"

using bambox::BamBox;

namespace {
// GTK ID's

// Screens
const char* GID_SCREEN_STACK = "screen-stack";
const char* GID_SCREEN_STACK_SPLASH_SCREEN = "splash-screen";
const char* GID_SCREEN_STACK_MAIN_SCREEN = "main-screen";

// Menu Buttons
const char* GID_MENU_BUTTONS_VOLUME = "volume-menu-button";
const char* GID_MENU_BUTTONS_OUTPUT = "output-menu-button";
const char* GID_MENU_BUTTONS_TRACKS = "tracks-menu-button";
const char* GID_MENU_BUTTONS_EJECT = "eject-menu-button";
const char* GID_MENU_BUTTONS_SETTINGS = "settings-menu-button";

// song info
const char* GID_SONG_INFO_ALBUM_ART = "album-art";
const char* GID_SONG_INFO_TRACK_NAME = "track-name-text";
const char* GID_SONG_INFO_ALBUM_TEXT = "album-text";
const char* GID_SONG_INFO_ARTIST_TEXT = "artist-text";
const char* GID_SONG_INFO_PROGRESS = "song-progress";

// Overlays
const char* GID_OVERLAY_VOLUME = "volume-overlay";
const char* GID_OVERLAY_OUTPUT = "output-overlay";
const char* GID_OVERLAY_TRACKS = "tracks-overlay";

// Volume overlay
const char* GID_OVERLAY_VOLUME_PROGRESS = "volume-progress";

// Output Overlay
const char* GID_OVERLAY_OUTPUT_LIST = "output-overlay-list";

const char* GID_OVERLAY_TRACKS_LIST = "tracks-overlay-list";

const char* DEFAULT_IMAGE_PATH = "res/logo.jpg";

}  // namespace

BamBox::BamBox() {}
BamBox::~BamBox() {}

bambox::Error BamBox::config(BamBoxConfig&& cfg) {
  spdlog::info("Configuring...");
  cfg_ = std::move(cfg);
  cd_reader_ = std::make_unique<CdReader>(cfg_.cd_mount_point);
  audio_player_ = std::make_unique<AudioPlayer>();
  gpio_ = std::make_shared<platform::Gpio>();
  lcd_display_ = std::make_unique<LcdDisplay>(gpio_);

  spdlog::info("Configuring GPIO");
  if (gpio_->init() != 0) {
    return {ECode::ERR_IO, "Failed it initialize GPIO are you root?"};
  }

  // Must be done after GPIO as it makes use of it
  spdlog::info("Configuring Display");
  auto res = lcd_display_->init();
  if (res.is_error()) {
    return {};
  }

  spdlog::info("Configuring Audio");
  for (const auto& audio_dev : cfg_.audio_devs) {
    audio_player_->create_device(audio_dev);
  }
  audio_player_->select_device(cfg_.default_audio_dev);

  return {};
}

void BamBox::cd_player_loop() {
  // On boot we need to show the splash screen
  std::this_thread::sleep_for(std::chrono::seconds(3));
  auto cb = (GSourceOnceFunc) + [](BamBox* bambox) {
    spdlog::info("setting visible child main");
    gtk_stack_set_visible_child_name(bambox->screen_stack_, GID_SCREEN_STACK_MAIN_SCREEN);
  };
  g_idle_add_once(cb, this);

  while (1) {
    {
      std::lock_guard<std::mutex> lk(mtx_);
      state_ = State::NO_DISC;
    }
    spdlog::info("Waiting for CD...");
    ui_update_track_info();
    ui_update_album_art();
    while (cd_reader_->wait_for_disc().is_error())
      ;

    {
      std::lock_guard<std::mutex> lk(mtx_);
      state_ = State::LOADING;
    }
    auto res = cd_reader_->load();
    if (res.is_error()) {
      spdlog::warn("Failed to load CD with: {}", res.str());
      continue;
    }

    cd_reader_->update_disc_info();
    {
      std::lock_guard<std::mutex> lk(mtx_);
      state_ = State::PLAYING;
    }
    auto disc = cd_reader_->get_disc();
    spdlog::info("Playing cd: \"{}\" by: \"{}\": id: {}", disc.title_, disc.artist_, cd_reader_->get_disc_id());

    cd_reader_->set_position(1);
    ui_update_track_info();
    ui_update_album_art();
    while (1) {
      auto track = disc.songs_[cd_reader_->get_track_number() - 1];
      spdlog::info("Playing track({}): \"{}\" by: {}", track.track_num_, track.title_, track.artist_);
      ui_update_track_info();

      {
        std::unique_lock<std::mutex> lk(mtx_);
        // If there was a seek request update the song info.
        if (seek_request_) {
          seek_request_ = false;
          continue;  // Update track info.
        }

        if (is_paused_) {
          spdlog::info("Song paused... cd thread going to sleep");
          cv_.wait(lk, [&]() { return state_ != State::PLAYING || !is_paused_ || seek_request_; });
          continue;  // continue to update track in case it changed when we were sleeping
        }
      }

      auto res = cd_reader_->play([&](const std::chrono::seconds& sec, void* data, int frames) -> int {
        ui_update_track_time(sec);
        audio_player_->write(data, frames);
        return 0;
      });

      {
        std::unique_lock<std::mutex> lk(mtx_);
        if (res.is_error()) {
          spdlog::warn("Failed to play cd with: {}", res.str());
          break;
        } else if (!is_paused_ && !seek_request_) {
          lk.unlock();
          res = next();
          if (res.is_error()) {
            spdlog::warn("Failed too play next songs with: {}", res.str());
          }
        }
      }
    }
  }
}

bambox::Error BamBox::go() {
  cd_thread_ = std::thread([this] { this->cd_player_loop(); });

  auto res = lcd_display_->go();
  if (res.is_error()) {
    return res;
  }

  gpio_->pull_mode_set(cfg_.next_gpio, platform::Gpio::PullMode::UP);
  gpio_->pull_mode_set(cfg_.prev_gpio, platform::Gpio::PullMode::UP);
  gpio_->pull_mode_set(cfg_.play_gpio, platform::Gpio::PullMode::UP);
  gpio_->alt_func_set(cfg_.play_gpio, platform::Gpio::AltFunc::INPUT);
  gpio_->alt_func_set(cfg_.prev_gpio, platform::Gpio::AltFunc::INPUT);
  gpio_->alt_func_set(cfg_.next_gpio, platform::Gpio::AltFunc::INPUT);

  gpio_->pull_mode_set(cfg_.rotary_encoder.button_gpio, platform::Gpio::PullMode::UP);
  gpio_->pull_mode_set(cfg_.rotary_encoder.clk_gpio, platform::Gpio::PullMode::UP);
  gpio_->pull_mode_set(cfg_.rotary_encoder.data_gpio, platform::Gpio::PullMode::UP);
  gpio_->alt_func_set(cfg_.rotary_encoder.button_gpio, platform::Gpio::AltFunc::INPUT);
  gpio_->alt_func_set(cfg_.rotary_encoder.clk_gpio, platform::Gpio::AltFunc::INPUT);
  gpio_->alt_func_set(cfg_.rotary_encoder.data_gpio, platform::Gpio::AltFunc::INPUT);

  gpio_->register_irq(
      cfg_.next_gpio, {platform::Gpio::TriggerType::FALLING_EDGE},
      [&](unsigned int gpio, bool high) {
        auto cb = (GSourceOnceFunc) + [](BamBox* bambox) { bambox->ui_handle_input(InputType::NEXT); };
        g_idle_add_once(cb, this);
      },
      std::chrono::milliseconds(500));

  gpio_->register_irq(
      cfg_.prev_gpio, {platform::Gpio::TriggerType::FALLING_EDGE},
      [&](unsigned int gpio, bool high) {
        auto cb = (GSourceOnceFunc) + [](BamBox* bambox) { bambox->ui_handle_input(InputType::PREV); };
        g_idle_add_once(cb, this);
      },
      std::chrono::milliseconds(500));

  gpio_->register_irq(
      cfg_.play_gpio, {platform::Gpio::TriggerType::FALLING_EDGE},
      [&](unsigned int gpio, bool high) {
        auto cb = (GSourceOnceFunc) + [](BamBox* bambox) { bambox->ui_handle_input(InputType::PLAY); };
        g_idle_add_once(cb, this);
      },
      std::chrono::milliseconds(500));

  gpio_->register_irq(
      cfg_.rotary_encoder.button_gpio, {platform::Gpio::TriggerType::RISING_EDGE},
      [&](unsigned int gpio, bool high) {
        spdlog::trace("Rotary Encoder pressed");
        auto cb = (GSourceOnceFunc) + [](BamBox* bambox) { bambox->ui_handle_input(InputType::PRESS); };
        g_idle_add_once(cb, this);
      },
      std::chrono::milliseconds(500));

  gpio_->register_irq(cfg_.rotary_encoder.clk_gpio,
                      {platform::Gpio::TriggerType::RISING_EDGE, platform::Gpio::TriggerType::FALLING_EDGE},
                      [&](unsigned int gpio, bool high) {
                        static bool old_state = false;

                        if (high != old_state) {
                          bool dt = gpio_->level_get(cfg_.rotary_encoder.data_gpio) != 0;
                          if (dt) {  // Only do one of the bumps to avoid incrementing twice per turn.
                            GSourceOnceFunc cb;
                            if (high == dt) {
                              cb =
                                  (GSourceOnceFunc) + [](BamBox* bambox) { bambox->ui_handle_input(InputType::RIGHT); };
                            } else {
                              cb = (GSourceOnceFunc) + [](BamBox* bambox) { bambox->ui_handle_input(InputType::LEFT); };
                            }
                            g_idle_add_once(cb, this);
                          }
                        }
                        old_state = high;
                      });

  // Start UI
  app_ = gtk_application_new("ca.larrycloud.bambox", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app_, "activate",
                   G_CALLBACK(+[](GtkApplication*, BamBox* bambox) -> void { bambox->ui_activate(); }), this);
  g_application_run(G_APPLICATION(app_), 0, nullptr);
  g_object_unref(G_APPLICATION(app_));

  return {};
}

bambox::Error BamBox::pause() {
  std::lock_guard<std::mutex> lk(mtx_);
  if (is_paused_ || (state_ != State::PLAYING)) {
    return {ECode::ERR_AGAIN, "pause(): Already paused or not playing."};
  }
  audio_player_->pause(false);
  cd_reader_->stop();
  is_paused_ = true;
  return {};
}

bambox::Error BamBox::resume() {
  std::lock_guard<std::mutex> lk(mtx_);
  if (!is_paused_ || (state_ != State::PLAYING)) {
    return {ECode::ERR_AGAIN, "resume(): Already playing or not disc."};
  }

  is_paused_ = false;
  audio_player_->pause(true);
  cv_.notify_all();
  return {};
}

bambox::Error BamBox::prev() {
  std::lock_guard<std::mutex> lk(mtx_);
  if (state_ != State::PLAYING) {
    return {ECode::ERR_INVAL_STATE, "Invalid state can't select prev track."};
  }

  if (!is_paused_) {
    cd_reader_->stop();
  }
  auto current_lba = cd_reader_->get_track_current_lba();
  auto start_lba = cd_reader_->get_track_start_lba();
  std::uint32_t three_sec_lba = MSF2LBA(0, 10, 0);
  std::int32_t track_num = cd_reader_->get_track_number();
  if ((current_lba - start_lba) <= three_sec_lba) {  // Play Previous track.
    track_num--;
    if (track_num <= 0) {
      track_num = cd_reader_->get_disc().songs_.size();
    }
  }  // Restart track

  auto res = cd_reader_->set_position(track_num);
  ui_update_track_time(std::chrono::seconds(0));
  seek_request_ = true;
  cv_.notify_all();
  return res;
}

bambox::Error BamBox::next() {
  std::lock_guard<std::mutex> lk(mtx_);
  if (state_ != State::PLAYING) {
    return {ECode::ERR_AGAIN, "Invalid state can't select prev track."};
  }

  auto track_num = cd_reader_->get_track_number() + 1;
  if (track_num > cd_reader_->get_disc().songs_.size()) {
    track_num = 1;
  }

  // We must stop the song
  if (!is_paused_) {
    cd_reader_->stop();
  }
  auto res = cd_reader_->set_position(track_num);
  ui_update_track_time(std::chrono::seconds(0));
  seek_request_ = true;
  cv_.notify_all();
  return res;
}

void BamBox::stop() {
  state_ = State::EXIT;
  cv_.notify_all();
  cd_thread_.join();
}

/***** UI CODE ******/
void BamBox::ui_activate() {
  GtkBuilder* builder = gtk_builder_new_from_file(cfg_.gtk_ui_path_.c_str());

  window_ = GTK_WINDOW(gtk_builder_get_object(builder, "window"));
  gtk_window_set_application(window_, app_);
  gtk_window_set_title(window_, "Bam-Box");
  gtk_window_fullscreen(window_);

  GtkCssProvider* provider = gtk_css_provider_new();
  GdkDisplay* display = gdk_display_get_default();
  gtk_css_provider_load_from_path(provider, cfg_.gtk_style_path_.c_str());
  gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  GtkIconTheme* icon_theme = gtk_icon_theme_get_for_display(display);

  // const char* search_path[] = {cfg_.gtk_icon_path_.c_str(), NULL};
  // gtk_icon_theme_set_search_path(icon_theme, search_path);

  const char* resource_path[] = {"/ca/larrycloud/bambox/icons", NULL};
  gtk_icon_theme_set_resource_path(icon_theme, resource_path);

  song_info_text_[0] = GTK_LABEL(gtk_builder_get_object(builder, GID_SONG_INFO_TRACK_NAME));
  song_info_text_[1] = GTK_LABEL(gtk_builder_get_object(builder, GID_SONG_INFO_ALBUM_TEXT));
  song_info_text_[2] = GTK_LABEL(gtk_builder_get_object(builder, GID_SONG_INFO_ARTIST_TEXT));

  menu_buttons_.push_back(GTK_BUTTON(gtk_builder_get_object(builder, GID_MENU_BUTTONS_VOLUME)));
  g_signal_connect(menu_buttons_.back(), "clicked", G_CALLBACK(+[](GtkButton* button, BamBox* bambox) -> void {
                     bambox->input_state_ = InputState::VOLUME;
                     gtk_progress_bar_set_fraction(bambox->volume_overlay_level_,
                                                   static_cast<double>(bambox->audio_player_->get_volume()) / 100);
                     gtk_widget_set_visible(bambox->volume_overlay_, true);
                     gtk_widget_set_opacity(bambox->volume_overlay_, 1.0);
                   }),
                   this);

  menu_buttons_.push_back(GTK_BUTTON(gtk_builder_get_object(builder, GID_MENU_BUTTONS_OUTPUT)));
  g_signal_connect(menu_buttons_.back(), "clicked", G_CALLBACK(+[](GtkButton* button, BamBox* bambox) -> void {
                     bambox->input_state_ = InputState::LIST;
                     gtk_list_box_remove_all(bambox->output_overlay_list_);
                     for (const auto& dev_name : bambox->audio_player_->get_device_names()) {
                       auto* button = gtk_button_new_with_label(dev_name.c_str());
                       gtk_widget_add_css_class(button, "menu-button");
                       gtk_widget_add_css_class(gtk_button_get_child(GTK_BUTTON(button)), "overlay-list-text");
                       gtk_list_box_append(bambox->output_overlay_list_, button);
                       g_signal_connect(button, "clicked", G_CALLBACK(+[](GtkButton* button, BamBox* bambox) -> void {
                                          bambox->audio_player_->select_device(gtk_button_get_label(button));
                                        }),
                                        bambox);
                     }

                     // select first row
                     bambox->list_box_select_index_ = 0;
                     auto row =
                         gtk_list_box_get_row_at_index(bambox->output_overlay_list_, bambox->list_box_select_index_);
                     gtk_widget_set_state_flags(gtk_list_box_row_get_child(row), GTK_STATE_FLAG_PRELIGHT, false);

                     gtk_widget_set_visible(bambox->output_overlay_, true);
                     gtk_widget_set_opacity(bambox->output_overlay_, 1.0);
                   }),
                   this);

  menu_buttons_.push_back(GTK_BUTTON(gtk_builder_get_object(builder, GID_MENU_BUTTONS_TRACKS)));
  menu_buttons_.push_back(GTK_BUTTON(gtk_builder_get_object(builder, GID_MENU_BUTTONS_EJECT)));
  g_signal_connect(menu_buttons_.back(), "clicked",
                   G_CALLBACK(+[](GtkButton* button, BamBox* bambox) -> void { bambox->cd_reader_->eject(); }), this);
  menu_buttons_.push_back(GTK_BUTTON(gtk_builder_get_object(builder, GID_MENU_BUTTONS_SETTINGS)));
  gtk_widget_set_state_flags(GTK_WIDGET(menu_buttons_.front()), GTK_STATE_FLAG_PRELIGHT, FALSE);
  for (auto* button : menu_buttons_) {
    gtk_widget_add_css_class((gtk_button_get_child(button)), "menu-button-icon");
    gtk_image_set_pixel_size(GTK_IMAGE(gtk_button_get_child(button)), 56);
  }

  song_progress_ = GTK_PROGRESS_BAR(gtk_builder_get_object(builder, GID_SONG_INFO_PROGRESS));
  album_art_ = GTK_IMAGE(gtk_builder_get_object(builder, GID_SONG_INFO_ALBUM_ART));

  screen_stack_ = GTK_STACK(gtk_builder_get_object(builder, GID_SCREEN_STACK));

  // Overlays
  volume_overlay_ = GTK_WIDGET(gtk_builder_get_object(builder, GID_OVERLAY_VOLUME));
  volume_overlay_level_ = GTK_PROGRESS_BAR(gtk_builder_get_object(builder, GID_OVERLAY_VOLUME_PROGRESS));

  output_overlay_ = GTK_WIDGET(gtk_builder_get_object(builder, GID_OVERLAY_OUTPUT));
  output_overlay_list_ = GTK_LIST_BOX(gtk_builder_get_object(builder, GID_OVERLAY_OUTPUT_LIST));

  gtk_window_present(window_);
  g_object_unref(builder);
}

void BamBox::ui_update_album_art() {
  auto cb = (GSourceOnceFunc)(+[](BamBox* bambox) {
    std::string art = DEFAULT_IMAGE_PATH;
    if (bambox->state_ == BamBox::State::PLAYING) {
      auto cd = bambox->cd_reader_->get_disc();
      if (!cd.album_art_path_.empty()) {
        art = cd.album_art_path_;
      }
    }
    gtk_image_set_from_file(bambox->album_art_, art.c_str());
  });

  g_idle_add_once(cb, this);
}

void BamBox::ui_update_track_info() {
  auto cb = (GSourceOnceFunc)(+[](BamBox* bambox) {
    std::string title = "";
    std::string artist = "";
    std::string album = "";

    if (bambox->state_ == BamBox::State::PLAYING) {
      auto song = bambox->cd_reader_->get_current_song();
      auto cd = bambox->cd_reader_->get_disc();

      artist = (!cd.artist_.empty()) ? cd.artist_ : "Unknown";
      album = (!cd.title_.empty()) ? cd.title_ : "Unknown";
      title = (!song.title_.empty()) ? song.title_ : ("Track " + std::to_string(song.track_num_));
      if (!song.artist_.empty()) {
        artist = song.artist_;
      }
    } else {
      title = "No CD...";
    }

    gtk_label_set_text(bambox->song_info_text_[0], title.c_str());
    // Disable album be able to increase font size.
    // gtk_label_set_text(bambox->song_info_text_[1], album.c_str());
    gtk_label_set_text(bambox->song_info_text_[2], artist.c_str());
  });

  g_idle_add_once(cb, this);
}

void BamBox::ui_update_track_time(const std::chrono::seconds sec) {
  current_time_ = sec;

  auto cb = (GSourceOnceFunc)(+[](BamBox* bambox) {
    double track_percent = 0;
    std::string time_text = "00:00/00:00";

    // TODO this isn't really thread safe we should have func which gives us both secs and length
    if (bambox->state_ == State::PLAYING) {
      auto song_length = bambox->cd_reader_->get_current_song().length_;
      track_percent = static_cast<double>(bambox->current_time_.count()) / song_length.count();

      // Cries in GCC12 :( R.I.P std::format
      std::ostringstream ss;
      ss << std::setw(2) << std::setfill('0')
         << std::chrono::duration_cast<std::chrono::minutes>(bambox->current_time_).count() << ":" << std::setw(2)
         << std::setfill('0') << (bambox->current_time_.count() % 60) << "/" << std::setw(2) << std::setfill('0')
         << std::chrono::duration_cast<std::chrono::minutes>(song_length).count() << ":" << std::setw(2)
         << std::setfill('0') << (song_length.count() % 60);
      time_text = ss.str();
    }

    gtk_progress_bar_set_fraction(bambox->song_progress_, track_percent);
    gtk_progress_bar_set_text(bambox->song_progress_, time_text.c_str());
  });
  g_idle_add_once(cb, this);
}

void BamBox::ui_handle_input(InputType type) {
  switch (input_state_) {
    case InputState::MAIN:
      ui_main_input(type);
      break;
    case InputState::VOLUME:
      ui_volume_input(type);
      break;
    case InputState::LIST:
      ui_list_input(type);
      break;
  }
}

void BamBox::ui_main_input(InputType type) {
  switch (type) {
    case InputType::LEFT:
    case InputType::RIGHT: {
      gtk_widget_unset_state_flags(GTK_WIDGET(menu_buttons_[selected_button_idx_]), GTK_STATE_FLAG_PRELIGHT);
      size_t inc = ((type == InputType::LEFT) ? -1 : 1);
      selected_button_idx_ = std::max(0UL, std::min(menu_buttons_.size() - 1UL, selected_button_idx_ + inc));
      gtk_widget_set_state_flags(GTK_WIDGET(menu_buttons_[selected_button_idx_]), GTK_STATE_FLAG_PRELIGHT, FALSE);
      break;
    }
    case InputType::PRESS:
      gtk_widget_activate(GTK_WIDGET(menu_buttons_[selected_button_idx_]));
      break;
    case InputType::PLAY: {
      bambox::Error res;
      if (!is_paused_) {
        spdlog::info("pausing song");
        res = pause();
      } else {
        spdlog::info("resuming song");
        res = resume();
      }
      if (res.is_error()) {
        spdlog::warn("Failed to play/pause with: {}", res.str());
      }
      break;
    }
    case InputType::PREV:
      prev();
      break;
    case InputType::NEXT:
      next();
      break;
  }
}

void BamBox::ui_list_input(InputType type) {
  switch (type) {
    case InputType::LEFT:
    case InputType::RIGHT: {
      auto row = gtk_list_box_get_row_at_index(output_overlay_list_, list_box_select_index_);
      gtk_widget_unset_state_flags(gtk_list_box_row_get_child(row), GTK_STATE_FLAG_PRELIGHT);
      size_t inc = ((type == InputType::LEFT) ? -1 : 1);

      // fix this is so bad
      list_box_select_index_ =
          std::max(0UL, std::min(audio_player_->get_device_names().size() - 1UL, list_box_select_index_ + inc));

      row = gtk_list_box_get_row_at_index(output_overlay_list_, list_box_select_index_);
      gtk_widget_set_state_flags(gtk_list_box_row_get_child(row), GTK_STATE_FLAG_PRELIGHT, false);
      break;
    }
    case InputType::PRESS:
      // todo change to generic list element.
      gtk_widget_activate(
          gtk_list_box_row_get_child(gtk_list_box_get_row_at_index(output_overlay_list_, list_box_select_index_)));
      gtk_widget_set_visible(output_overlay_, false);
      input_state_ = InputState::MAIN;
      break;
    case InputType::PLAY:
    case InputType::PREV:
    case InputType::NEXT:
      break;
  }
}

void BamBox::ui_volume_input(InputType type) {
  switch (type) {
    case InputType::LEFT:
    case InputType::RIGHT: {
      int vol = static_cast<int>(audio_player_->get_volume()) + ((type == InputType::RIGHT) ? 1 : -1);
      audio_player_->set_volume(std::max(vol, 0));
      gtk_progress_bar_set_fraction(volume_overlay_level_, static_cast<double>(vol) / 100);
      break;
    }
    case InputType::PRESS:
      gtk_widget_set_visible(volume_overlay_, false);
      input_state_ = InputState::MAIN;
      break;
    // do nothing
    case InputType::PLAY:
    case InputType::PREV:
    case InputType::NEXT:
      break;
  }
}