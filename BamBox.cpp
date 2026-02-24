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
const char* GID_SCREEN_STACK_SETTING_SCREEN = "setting-screen";

// Menu Buttons
const char* GID_MENU_BUTTONS_VOLUME = "volume-menu-button";
const char* GID_MENU_BUTTONS_OUTPUT = "output-menu-button";
const char* GID_MENU_BUTTONS_TRACKS = "tracks-menu-button";
const char* GID_MENU_BUTTONS_EJECT = "eject-menu-button";
const char* GID_MENU_BUTTONS_SETTINGS = "settings-menu-button";

// Settings Buttons
const char* GID_SETTING_WIN = "setting-win";
const char* GID_SETTING_BUTTONS_VOLUME = "volume-settings-button";
const char* GID_SETTING_BUTTONS_OUTPUT = "output-settings-button";
const char* GID_SETTING_BUTTONS_THEME = "theme-settings-button";
const char* GID_SETTING_BUTTONS_DUMP = "dump-settings-button";
const char* GID_SETTING_BUTTONS_CD_INFO = "cd-info-setting-button";
const char* GID_SETTING_BUTTONS_ABOUT = "about-setting-button";
const char* GID_SETTING_BUTTONS_RESTART = "restart-setting-button";

// Settings Labels
const char* GID_SETTING_LABEL_OUTPUT = "output-settings-label";
const char* GID_SETTING_LABEL_VOLUME = "volume-settings-label";
const char* GID_SETTING_SWITCH_THEME = "theme-settings-switch";
const char* GID_SETTING_LABEL_DUMP = "dump-settings-label";

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

// Settings Overlays
const char* GID_SETTING_OVERLAY_OUTPUT = "setting-output-overlay";
const char* GID_SETTING_OVERLAY_VOLUME = "setting-volume-overlay";
const char* GID_SETTING_OVERLAY_DUMP = "setting-dump-overlay";
const char* GID_SETTING_OVERLAY_CD_INFO = "setting-cd-info-overlay";
const char* GID_SETTING_OVERLAY_ABOUT = "setting-about-overlay";

// Volume overlay
const char* GID_OVERLAY_VOLUME_PROGRESS = "volume-progress";

// Output Overlay
const char* GID_OVERLAY_OUTPUT_LIST = "output-overlay-list";

// Tracks Overlay
const char* GID_OVERLAY_TRACKS_LIST = "tracks-overlay-list";
const char* GID_OVERLAY_TRACKS_WIN = "tracks-overlay-window";

// Settings - Audio Device
const char* GID_SETTING_OVERLAY_OUTPUT_LIST = "setting-output-overlay-list";

// Settings - Volume
const char* GID_SETTING_OVERLAY_VOLUME_PROGRESS = "setting-volume-progress";

// Settings - CD Info Overlay
const char* GID_SETTING_OVERLAY_CD_INFO_ALBUM_NAME = "cd-info-album-name";
const char* GID_SETTING_OVERLAY_CD_INFO_ARTIST_NAME = "cd-info-artist-name";
const char* GID_SETTING_OVERLAY_CD_INFO_TRACK_COUNT = "cd-info-track-count";
const char* GID_SETTING_OVERLAY_CD_INFO_ALBUM_LENGTH = "cd-info-length";
const char* GID_SETTING_OVERLAY_CD_INFO_RELEASE_DATE = "cd-info-release-date";
const char* GID_SETTING_OVERLAY_CD_INFO_ALBUM_ART = "cd-info-album-art";

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

bambox::Error BamBox::next(int track) {
  std::lock_guard<std::mutex> lk(mtx_);
  if (state_ != State::PLAYING) {
    return {ECode::ERR_AGAIN, "Invalid state can't select prev track."};
  }

  auto track_num = (track == -1) ? (cd_reader_->get_track_number() + 1) : track;
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
  GtkBuilder* builder = gtk_builder_new_from_resource(cfg_.gtk_ui_path_.c_str());

  window_ = GTK_WINDOW(gtk_builder_get_object(builder, "window"));
  gtk_window_set_application(window_, app_);
  gtk_window_set_title(window_, "Bam-Box");
  gtk_window_fullscreen(window_);

  // CSS Theming
  GtkCssProvider* colour_provider = gtk_css_provider_new();
  GtkCssProvider* provider = gtk_css_provider_new();
  GdkDisplay* display = gdk_display_get_default();
  const auto& css_path = (cfg_.dark_mode) ? cfg_.gtk_style_dark_path_ : cfg_.gtk_style_light_path_;
  gtk_css_provider_load_from_resource(colour_provider, css_path.c_str());
  gtk_css_provider_load_from_resource(provider, cfg_.gtk_style_path_.c_str());
  gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(colour_provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  // Icons Theming
  GtkIconTheme* icon_theme = gtk_icon_theme_get_for_display(display);
  const char* resource_path[] = {"/ca/larrycloud/bambox/icons", NULL};
  gtk_icon_theme_set_resource_path(icon_theme, resource_path);

  /******* MENU BUTTONS  **************/

  menu_buttons_.add_button(
      std::make_unique<ui::BamBoxButton>(builder, GID_MENU_BUTTONS_VOLUME, [&](auto* gtk_button, auto* button) {
        volume_overlay_level_->init(audio_player_->get_volume());
        active_slider_ = volume_overlay_level_;
        ui_show_overlay(volume_overlay_, InputState::VOLUME);
      }));
  menu_buttons_.add_button(
      std::make_unique<ui::BamBoxButton>(builder, GID_MENU_BUTTONS_OUTPUT, [&](auto* gtk_button, auto* button) {
        output_overlay_list_->clear();
        for (const auto& dev_name : audio_player_->get_device_names()) {
          output_overlay_list_->add_label(dev_name.c_str());
        }

        ui_show_overlay(output_overlay_, InputState::LIST);
        ui_set_list(output_overlay_list_);
      }));

  menu_buttons_.add_button(
      std::make_unique<ui::BamBoxButton>(builder, GID_MENU_BUTTONS_TRACKS, [&](auto* gtk_button, auto* button) {
        if (cd_reader_->get_disc().songs_.size() == 0) {
          return;  // no songs
        }
        tracks_overlay_list_->clear();
        for (const auto& track : cd_reader_->get_disc().songs_) {
          tracks_overlay_list_->add_label(track.title_.c_str());
        }

        ui_show_overlay(tracks_overlay_, InputState::LIST);
        ui_set_list(tracks_overlay_list_, cd_reader_->get_track_number() - 1);
      }));
  menu_buttons_.add_button(std::make_unique<ui::BamBoxButton>(
      builder, GID_MENU_BUTTONS_EJECT, [&](auto* gtk_button, auto* button) { cd_reader_->eject(); }));

  menu_buttons_.add_button(
      std::make_unique<ui::BamBoxButton>(builder, GID_MENU_BUTTONS_SETTINGS, [&](auto* gtk_button, auto* button) {
        // Set values in config
        gtk_switch_set_active(settting_theme_switch_, cfg_.dark_mode);
        gtk_label_set_label(settting_output_label_, fmt::format("({})", cfg_.default_audio_dev).c_str());

        // Find the default audio dev to get the volume.
        const auto& devs = cfg_.audio_devs;
        auto dev = std::find_if(devs.begin(), devs.end(),
                                [&](const auto& d) { return d.display_name == cfg_.default_audio_dev; });
        gtk_label_set_label(settting_volume_label_, fmt::format("({}%)", dev->volume).c_str());

        // Display setting screen after values are set.
        std::string stack_name = gtk_stack_get_visible_child_name(screen_stack_);
        gtk_stack_set_visible_child_name(screen_stack_, GID_SCREEN_STACK_SETTING_SCREEN);

        setting_buttons_.select(0);
        selected_button_ = &setting_buttons_;  // TODO this should probably be a stack.
        ui_push_stack(InputState::BUTTON_GROUP, [this, stack_name] {
          gtk_stack_set_visible_child_name(screen_stack_, stack_name.c_str());
          // Because it lost focus it will no longer be select so re select it.
          selected_button_ = &menu_buttons_;
          menu_buttons_.select(menu_buttons_.get_selected_idx());
        });
      }));
  menu_buttons_.select(0);

  /************ SONG INFO **************/
  song_info_text_[0] = GTK_LABEL(gtk_builder_get_object(builder, GID_SONG_INFO_TRACK_NAME));
  song_info_text_[1] = GTK_LABEL(gtk_builder_get_object(builder, GID_SONG_INFO_ALBUM_TEXT));
  song_info_text_[2] = GTK_LABEL(gtk_builder_get_object(builder, GID_SONG_INFO_ARTIST_TEXT));
  song_progress_ =
      std::make_shared<ui::BamBoxSlider>(builder, GID_SONG_INFO_PROGRESS, nullptr, nullptr, [&](int value) {
        // Cries in GCC12 :( R.I.P std::format
        auto song_length = cd_reader_->get_current_song().length_;
        std::ostringstream ss;
        ss << std::setw(2) << std::setfill('0')
           << std::chrono::duration_cast<std::chrono::minutes>(current_time_).count() << ":" << std::setw(2)
           << std::setfill('0') << (current_time_.count() % 60) << "/" << std::setw(2) << std::setfill('0')
           << std::chrono::duration_cast<std::chrono::minutes>(song_length).count() << ":" << std::setw(2)
           << std::setfill('0') << (song_length.count() % 60);
        return ss.str();
      });
  album_art_ = GTK_IMAGE(gtk_builder_get_object(builder, GID_SONG_INFO_ALBUM_ART));

  screen_stack_ = GTK_STACK(gtk_builder_get_object(builder, GID_SCREEN_STACK));

  /************ OVERLAYS **************/
  volume_overlay_ = GTK_WIDGET(gtk_builder_get_object(builder, GID_OVERLAY_VOLUME));
  volume_overlay_level_ = std::make_shared<ui::BamBoxSlider>(
      builder, GID_OVERLAY_VOLUME_PROGRESS, [&](int value) { audio_player_->set_volume(std::max(value, 0)); }, nullptr,
      nullptr);

  output_overlay_ = GTK_WIDGET(gtk_builder_get_object(builder, GID_OVERLAY_OUTPUT));
  output_overlay_list_ = std::make_shared<ui::BamBoxList>(
      builder, GID_OVERLAY_OUTPUT_LIST, nullptr, [&](ui::BamBoxButton& button, int idx) {
        audio_player_->select_device(gtk_button_get_label(button.as_button()));
      });

  tracks_overlay_ = GTK_WIDGET(gtk_builder_get_object(builder, GID_OVERLAY_TRACKS));
  tracks_overlay_list_ = std::make_shared<ui::BamBoxList>(builder, GID_OVERLAY_TRACKS_LIST, GID_OVERLAY_TRACKS_WIN,
                                                          [&](ui::BamBoxButton& button, int idx) { next(idx + 1); });

  /******* SETTING BUTTONS  **************/
  setting_win_ = GTK_SCROLLED_WINDOW(GTK_BUTTON(gtk_builder_get_object(builder, GID_SETTING_WIN)));
  setting_buttons_.add_onhover([&](ui::BamBoxButton& button, int position) {
    // Make sure we can see the buttons off screen
    GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(setting_win_);
    double current = gtk_adjustment_get_value(vadj);
    double page = gtk_adjustment_get_page_size(vadj);

    graphene_rect_t rect;
    (void)gtk_widget_compute_bounds(setting_buttons_.selected()->as_widget(), GTK_WIDGET(setting_win_), &rect);
    if (rect.origin.y < current) {
      gtk_adjustment_set_value(vadj, rect.origin.y);
    } else if (current + page < rect.origin.y + rect.size.height + 10) {
      gtk_adjustment_set_value(vadj, rect.origin.y + rect.size.height - page);
    }
  });

  setting_buttons_.add_button(
      std::make_unique<ui::BamBoxButton>(builder, GID_SETTING_BUTTONS_OUTPUT, [&](auto* gtk_button, auto* button) {
        settings_output_overlay_list_->clear();
        for (const auto& dev_name : audio_player_->get_device_names()) {
          settings_output_overlay_list_->add_label(dev_name.c_str());
        }

        ui_show_overlay(settings_output_overlay_, InputState::LIST);
        ui_set_list(settings_output_overlay_list_);
      }));

  setting_buttons_.add_button(
      std::make_unique<ui::BamBoxButton>(builder, GID_SETTING_BUTTONS_VOLUME, [&](auto* gtk_button, auto* button) {
        auto& devs = cfg_.audio_devs;
        auto dev = std::find_if(devs.begin(), devs.end(),
                                [&](const auto& d) { return d.display_name == cfg_.default_audio_dev; });
        settings_volume_slider_->init(dev->volume);
        active_slider_ = settings_volume_slider_;
        ui_show_overlay(settings_volume_overlay_, InputState::VOLUME);
      }));

  setting_buttons_.add_button(
      std::make_unique<ui::BamBoxButton>(builder, GID_SETTING_BUTTONS_THEME, [&](auto* gtk_button, auto* button) {
        cfg_.dark_mode = !cfg_.dark_mode;
        dump_config(cfg_);
        gtk_switch_set_active(settting_theme_switch_, cfg_.dark_mode);
      }));
  setting_buttons_.add_button(std::make_unique<ui::BamBoxButton>(
      builder, GID_SETTING_BUTTONS_DUMP,
      [&](auto* gtk_button, auto* button) { ui_show_overlay(settings_dump_overlay_, InputState::INFO); }));
  setting_buttons_.add_button(
      std::make_unique<ui::BamBoxButton>(builder, GID_SETTING_BUTTONS_CD_INFO, [&](auto* gtk_button, auto* button) {
        auto cd = cd_reader_->get_disc();

        std::chrono::seconds length{0};
        for (const auto& song : cd.songs_) {
          length += song.length_;
        }

        std::ostringstream ss;
        ss << std::setw(2) << std::setfill('0') << std::chrono::duration_cast<std::chrono::hours>(length).count() << ":"
           << std::setw(2) << (std::chrono::duration_cast<std::chrono::minutes>(length).count() % 60) << ":"
           << std::setw(2) << std::setfill('0') << (length.count() % 60);

        gtk_label_set_label(cd_info_album_name_, cd.title_.c_str());
        gtk_label_set_label(cd_info_artist_name_, cd.artist_.c_str());
        gtk_label_set_label(cd_info_track_count_, std::to_string(cd.songs_.size()).c_str());
        gtk_label_set_label(cd_info_album_length_, ss.str().c_str());
        gtk_label_set_label(cd_info_release_date_, cd.release_date_.c_str());

        if (!cd.album_art_path_.empty()) {
          gtk_image_set_from_file(cd_info_album_art_, cd.album_art_path_.c_str());
        } else {
          gtk_image_set_from_resource(cd_info_album_art_, "/ca/larrycloud/bambox/icons/48x48/actions/logo.jpg");
        }
        ui_show_overlay(settings_cd_info_overlay_, InputState::INFO);
      }));

  setting_buttons_.add_button(std::make_unique<ui::BamBoxButton>(
      builder, GID_SETTING_BUTTONS_ABOUT,
      [&](auto* gtk_button, auto* button) { ui_show_overlay(settings_about_overlay_, InputState::INFO); }));
  setting_buttons_.add_button(std::make_unique<ui::BamBoxButton>(builder, GID_SETTING_BUTTONS_RESTART,
                                                                 [&](auto* gtk_button, auto* button) { exit(0); }));

  /******* SETTING LABELS **************/
  settting_output_label_ = GTK_LABEL(gtk_builder_get_object(builder, GID_SETTING_LABEL_OUTPUT));
  settting_volume_label_ = GTK_LABEL(gtk_builder_get_object(builder, GID_SETTING_LABEL_VOLUME));
  settting_theme_switch_ = GTK_SWITCH(gtk_builder_get_object(builder, GID_SETTING_SWITCH_THEME));
  settting_dump_label_ = GTK_LABEL(gtk_builder_get_object(builder, GID_SETTING_LABEL_DUMP));

  /******* SETTING OVERLAYS **************/
  settings_about_overlay_ = GTK_WIDGET(gtk_builder_get_object(builder, GID_SETTING_OVERLAY_ABOUT));
  settings_dump_overlay_ = GTK_WIDGET(gtk_builder_get_object(builder, GID_SETTING_OVERLAY_DUMP));

  settings_volume_overlay_ = GTK_WIDGET(gtk_builder_get_object(builder, GID_SETTING_OVERLAY_VOLUME));
  settings_volume_slider_ = std::make_shared<ui::BamBoxSlider>(
      builder, GID_SETTING_OVERLAY_VOLUME_PROGRESS, nullptr,
      [&](int val) {
        auto& devs = cfg_.audio_devs;
        auto dev = std::find_if(devs.begin(), devs.end(),
                                [&](const auto& d) { return d.display_name == cfg_.default_audio_dev; });
        dev->volume = val;
        gtk_label_set_label(settting_volume_label_, fmt::format("({}%)", dev->volume).c_str());
        dump_config(cfg_);
      },
      nullptr);

  settings_output_overlay_ = GTK_WIDGET(gtk_builder_get_object(builder, GID_SETTING_OVERLAY_OUTPUT));
  settings_output_overlay_list_ = std::make_shared<ui::BamBoxList>(
      builder, GID_SETTING_OVERLAY_OUTPUT_LIST, nullptr, [&](ui::BamBoxButton& button, int idx) {
        cfg_.default_audio_dev = gtk_button_get_label(button.as_button());
        dump_config(cfg_);

        // Update both the default volume and output
        auto& devs = cfg_.audio_devs;
        auto dev = std::find_if(devs.begin(), devs.end(),
                                [&](const auto& d) { return d.display_name == cfg_.default_audio_dev; });
        gtk_label_set_label(settting_volume_label_, fmt::format("({}%)", dev->volume).c_str());
        gtk_label_set_label(settting_output_label_, fmt::format("({})", cfg_.default_audio_dev).c_str());
      });

  settings_cd_info_overlay_ = GTK_WIDGET(gtk_builder_get_object(builder, GID_SETTING_OVERLAY_CD_INFO));

  cd_info_album_name_ = GTK_LABEL(gtk_builder_get_object(builder, GID_SETTING_OVERLAY_CD_INFO_ALBUM_NAME));
  cd_info_artist_name_ = GTK_LABEL(gtk_builder_get_object(builder, GID_SETTING_OVERLAY_CD_INFO_ARTIST_NAME));
  cd_info_track_count_ = GTK_LABEL(gtk_builder_get_object(builder, GID_SETTING_OVERLAY_CD_INFO_TRACK_COUNT));
  cd_info_album_length_ = GTK_LABEL(gtk_builder_get_object(builder, GID_SETTING_OVERLAY_CD_INFO_ALBUM_LENGTH));
  cd_info_release_date_ = GTK_LABEL(gtk_builder_get_object(builder, GID_SETTING_OVERLAY_CD_INFO_RELEASE_DATE));
  cd_info_album_art_ = GTK_IMAGE(gtk_builder_get_object(builder, GID_SETTING_OVERLAY_CD_INFO_ALBUM_ART));
  gtk_window_present(window_);
  g_object_unref(builder);
}

void BamBox::ui_update_album_art() {
  auto cb = (GSourceOnceFunc)(+[](BamBox* bambox) {
    std::string art = DEFAULT_IMAGE_PATH;  // TODO change to resource
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
      track_percent = static_cast<int>(100 * bambox->current_time_.count()) / song_length.count();
    }

    bambox->song_progress_->init(track_percent);
  });
  g_idle_add_once(cb, this);
}

void BamBox::ui_handle_input(InputType type) {
  switch (input_state_) {
    case InputState::BUTTON_GROUP:
      ui_button_group_input(type);
      break;
    case InputState::VOLUME:
      ui_volume_input(type);
      break;
    case InputState::LIST:
      ui_list_input(type);
      break;
    case InputState::INFO:
      ui_info_input(type);
      break;
  }
}

void BamBox::ui_button_group_input(InputType type) {
  switch (type) {
    case InputType::LEFT:
      selected_button_->prev();
      break;
    case InputType::RIGHT:
      selected_button_->next();
      break;
    case InputType::PRESS:
      selected_button_->click();
      break;
    case InputType::PLAY: {
      if (selected_button_ == &menu_buttons_) {
        bambox::Error res;
        if (!is_paused_) {
          spdlog::info("Pausing song");
          res = pause();
        } else {
          spdlog::info("Resuming song");
          res = resume();
        }
        if (res.is_error()) {
          spdlog::warn("Failed to play/pause with: {}", res.str());
        }
      } else {
        ui_pop_stack();
      }
      break;
    }
    case InputType::PREV:
      if (selected_button_ == &menu_buttons_) {
        prev();
      } else {
        ui_pop_stack();
      }
      break;
    case InputType::NEXT:
      if (selected_button_ == &menu_buttons_) {
        next();
      } else {
        ui_pop_stack();
      }
      break;
  }
}

void BamBox::ui_info_input(InputType type) {
  switch (type) {
    case InputType::LEFT:
    case InputType::RIGHT: {
      break;
      case InputType::PRESS:
      case InputType::PLAY:
      case InputType::PREV:
      case InputType::NEXT:
        // on any input just pop the stack.
        ui_pop_stack();
        break;
    }
  }
}

void BamBox::ui_list_input(InputType type) {
  switch (type) {
    case InputType::LEFT:
      active_list_->prev();
      break;
    case InputType::RIGHT:
      active_list_->next();
      break;
    case InputType::PRESS:
      active_list_->activate();
      ui_pop_stack();
      break;
    // Any button cancels input.
    case InputType::PLAY:
    case InputType::PREV:
    case InputType::NEXT:
      ui_pop_stack();
      break;
  }
}

void BamBox::ui_volume_input(InputType type) {
  switch (type) {
    case InputType::LEFT:
    case InputType::RIGHT: {
      int vol = static_cast<int>(active_slider_->value()) + ((type == InputType::RIGHT) ? 1 : -1);
      vol = std::min(vol, 100);
      vol = std::max(vol, 0);
      active_slider_->init(vol);
      break;
    }
    case InputType::PRESS:
      active_slider_->commit();
      // FALLTHRU
    case InputType::PLAY:
    case InputType::PREV:
    case InputType::NEXT:
      ui_pop_stack();
      break;
  }
}
