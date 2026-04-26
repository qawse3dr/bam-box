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
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>

#include "AudioPlayer.hpp"
#include "BamBoxError.hpp"
#include "CdReader.hpp"
#include "FlacWriter.hpp"
#include "platform/Gpio.hpp"
#include "util/WebDAV.hpp"

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

// Settings - DUMP
const char* GID_SETTING_OVERLAY_DUMP_CANCEL = "dump-cancel-button";
const char* GID_SETTING_OVERLAY_DUMP_ACCEPT = "dump-accept-button";
const char* GID_SETTING_OVERLAY_ALBUM_LABEL = "settings-dump-overlay-title";
const char* GID_SETTING_OVERLAY_TRACK_LABEL = "settings-dump-overlay-tracks";
const char* GID_SETTING_OVERLAY_SONG_PROGRESS = "dump-song-progress";
const char* GID_SETTING_OVERLAY_DISC_PROGRESS = "dump-disc-progress";

// Settings - Volume
const char* GID_SETTING_OVERLAY_VOLUME_PROGRESS = "setting-volume-progress";

// Settings - CD Info Overlay
const char* GID_SETTING_OVERLAY_CD_INFO_ALBUM_NAME = "cd-info-album-name";
const char* GID_SETTING_OVERLAY_CD_INFO_ARTIST_NAME = "cd-info-artist-name";
const char* GID_SETTING_OVERLAY_CD_INFO_TRACK_COUNT = "cd-info-track-count";
const char* GID_SETTING_OVERLAY_CD_INFO_ALBUM_LENGTH = "cd-info-length";
const char* GID_SETTING_OVERLAY_CD_INFO_RELEASE_DATE = "cd-info-release-date";
const char* GID_SETTING_OVERLAY_CD_INFO_ALBUM_ART = "cd-info-album-art";

const char* DEFAULT_IMAGE_PATH = "/ca/larrycloud/bambox/ui/res/logo.jpg";

}  // namespace

BamBox::BamBox() {}
BamBox::~BamBox() {}

bambox::Error BamBox::config(BamBoxConfig&& cfg) {
  spdlog::info("Configuring...");
  cfg_ = std::move(cfg);
  cd_reader_ = std::make_shared<CdReader>(cfg_);
  audio_player_ = std::make_shared<AudioPlayer>();
  cd_player_ = std::make_unique<CdPlayer>(
      cd_reader_, audio_player_,
      std::bind(&BamBox::cd_player_event, this, std::placeholders::_1, std::placeholders::_2));
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

void BamBox::cd_player_event(CdPlayer::Event e, const CdPlayer::EventData& data) {
  switch (e) {
    case CdPlayer::Event::STARTUP: {
      spdlog::info("cd_event: STARTUP");
      auto cb = (GSourceOnceFunc) + [](BamBox* bambox) {
        spdlog::info("setting visible child main");
        gtk_stack_set_visible_child_name(bambox->screen_stack_, GID_SCREEN_STACK_MAIN_SCREEN);
      };
      g_timeout_add_once(3000, cb, this);

      {
        // Run in task to avoid circular lock.
        GTask* task = g_task_new(nullptr, nullptr, nullptr, cd_player_.get());
        g_task_set_task_data(task, cd_player_.get(), nullptr);
        g_task_run_in_thread(
            task, (GTaskThreadFunc) + [](GTask* task, gpointer source_object, bambox::CdPlayer* cd_player,
                                         GCancellable* cancellable) { cd_player->load(); });
        g_object_unref(task);
      }
      break;
    }
    case CdPlayer::Event::CD_EJECTED:
      spdlog::info("cd_event: EJECTED {}", std::get<Error>(data).str());
      // reset cd
      current_cd_ = CdReader::CD();
      ui_update_track_info();
      ui_update_album_art();
      {
        // Run in task to avoid circular lock.
        GTask* task = g_task_new(nullptr, nullptr, nullptr, nullptr);
        g_task_set_task_data(task, cd_player_.get(), nullptr);
        g_task_run_in_thread(
            task, (GTaskThreadFunc) + [](GTask* task, gpointer source_object, bambox::CdPlayer* cd_player,
                                         GCancellable* cancellable) { cd_player->load(); });
        g_object_unref(task);
      }
      break;
    case CdPlayer::Event::CD_LOADED:
      spdlog::info("cd_event: LOADED");
      current_cd_ = std::get<CdReader::CD>(data);
      cd_reader_->set_position(1);
      current_song_ = current_cd_.songs_[0];
      ui_update_track_info();
      ui_update_album_art();
      cd_player_->play();
      spdlog::trace("cd_event: LOADED END");
      break;
    case CdPlayer::Event::CD_TRACK_ENDED: {
      spdlog::trace("cd_event: TRACK_ENDED");

      auto track_num = (current_song_.track_num_ + 1);
      if (track_num > current_cd_.songs_.size()) {
        track_num = 1;
      }
      current_song_ = current_cd_.songs_[track_num - 1];
      auto res = cd_reader_->set_position(track_num);
      ui_update_track_info();
      break;
    }
    case CdPlayer::Event::CD_TIME_UPDATE:
      ui_update_track_time(std::get<std::chrono::seconds>(data));
      break;
  }
}

bambox::Error BamBox::go() {
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

bambox::Error BamBox::prev() {
  cd_player_->pause();
  // todo update to just use current_ts
  auto current_lba = cd_reader_->get_track_current_lba();
  auto start_lba = cd_reader_->get_track_start_lba();
  std::uint32_t three_sec_lba = MSF2LBA(0, 10, 0);
  std::int32_t track_num = cd_reader_->get_track_number();
  if ((current_lba - start_lba) <= three_sec_lba) {  // Play Previous track.
    track_num--;
    if (track_num <= 0) {
      track_num = current_cd_.songs_.size();
    }
  }  // Restart track

  auto res = cd_reader_->set_position(track_num);
  current_song_ = current_cd_.songs_[track_num - 1];
  ui_update_track_time(std::chrono::seconds(0));
  ui_update_track_info();
  cd_player_->play();
  return res;
}

bambox::Error BamBox::next(int track) {
  auto track_num = (track == -1) ? (current_song_.track_num_ + 1) : track;
  if (track_num > current_cd_.songs_.size()) {
    track_num = 1;
  }

  cd_player_->pause();
  auto res = cd_reader_->set_position(track_num);
  current_song_ = current_cd_.songs_[track_num - 1];
  ui_update_track_info();
  cd_player_->play();
  return res;
}

void BamBox::stop() {}

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
        if (current_cd_.songs_.size() == 0) {
          return;  // no songs
        }
        tracks_overlay_list_->clear();
        for (const auto& track : current_cd_.songs_) {
          // TODO default if track name isn't found
          tracks_overlay_list_->add_label(track.title_.c_str());
        }

        ui_show_overlay(tracks_overlay_, InputState::LIST);
        ui_set_list(tracks_overlay_list_, current_song_.track_num_ - 1);
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
        auto song_length = current_song_.length_;
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
  setting_buttons_.add_button(
      std::make_unique<ui::BamBoxButton>(builder, GID_SETTING_BUTTONS_DUMP, [&](auto* gtk_button, auto* button) {
        dump_buttons_.select(0);
        selected_button_ = &dump_buttons_;
        active_overlay_ = settings_dump_overlay_;
        gtk_label_set_label(dump_album_name_, current_cd_.title_.c_str());
        gtk_label_set_label(dump_track_count_, std::to_string(current_cd_.songs_.size()).c_str());
        dump_song_progress_slider_->init(0);
        dump_disc_progress_slider_->init(0);
        gtk_widget_set_visible(active_overlay_, true);
        gtk_widget_set_opacity(active_overlay_, 1.0);
        ui_push_stack(InputState::BUTTON_GROUP, [this] {
          // Because it lost focus it will no longer be select so re select it.
          selected_button_ = &setting_buttons_;
          setting_buttons_.select(setting_buttons_.get_selected_idx());
          if (dump_thread_.joinable()) {
            dump_thread_.join();
          }
          ui_hide_overlay();
        });
      }));
  setting_buttons_.add_button(
      std::make_unique<ui::BamBoxButton>(builder, GID_SETTING_BUTTONS_CD_INFO, [&](auto* gtk_button, auto* button) {
        std::chrono::seconds length{0};
        for (const auto& song : current_cd_.songs_) {
          length += song.length_;
        }

        std::ostringstream ss;
        ss << std::setw(2) << std::setfill('0') << std::chrono::duration_cast<std::chrono::hours>(length).count() << ":"
           << std::setw(2) << (std::chrono::duration_cast<std::chrono::minutes>(length).count() % 60) << ":"
           << std::setw(2) << std::setfill('0') << (length.count() % 60);

        gtk_label_set_label(cd_info_album_name_, current_cd_.title_.c_str());
        gtk_label_set_label(cd_info_artist_name_, current_cd_.artist_.c_str());
        gtk_label_set_label(cd_info_track_count_, std::to_string(current_cd_.songs_.size()).c_str());
        gtk_label_set_label(cd_info_album_length_, ss.str().c_str());
        gtk_label_set_label(cd_info_release_date_, current_cd_.release_date_.c_str());

        if (!current_cd_.album_art_path_.empty()) {
          gtk_image_set_from_file(cd_info_album_art_, current_cd_.album_art_path_.c_str());
        } else {
          gtk_image_set_from_resource(cd_info_album_art_, DEFAULT_IMAGE_PATH);
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
  dump_album_name_ = GTK_LABEL(gtk_builder_get_object(builder, GID_SETTING_OVERLAY_ALBUM_LABEL));
  dump_track_count_ = GTK_LABEL(gtk_builder_get_object(builder, GID_SETTING_OVERLAY_TRACK_LABEL));
  dump_song_progress_slider_ =
      std::make_shared<ui::BamBoxSlider>(builder, GID_SETTING_OVERLAY_SONG_PROGRESS, nullptr, nullptr, nullptr);
  dump_disc_progress_slider_ =
      std::make_shared<ui::BamBoxSlider>(builder, GID_SETTING_OVERLAY_DISC_PROGRESS, nullptr, nullptr, nullptr);

  dump_buttons_.add_button(
      std::make_unique<ui::BamBoxButton>(builder, GID_SETTING_OVERLAY_DUMP_CANCEL, [&](auto* gtk_button, auto* button) {
        if (dump_thread_.joinable()) {
          return;
        }
        ui_pop_stack();
      }));
  dump_buttons_.add_button(
      std::make_unique<ui::BamBoxButton>(builder, GID_SETTING_OVERLAY_DUMP_ACCEPT, [&](auto* gtk_button, auto* button) {
        if (dump_thread_.joinable()) {
          return;
        }
        dump_thread_ = std::thread([&]() {
          std::chrono::seconds prev_sec(1000000000);

          // Create the directories needed to upload the files
          WebDAV webDAV(cfg_.webdav.url, cfg_.webdav.user, cfg_.webdav.pass);
          webDAV.create_dir(current_cd_.artist_.c_str());
          std::string upload_folder = fmt::format("{}/{}", current_cd_.artist_, current_cd_.title_);
          webDAV.create_dir(upload_folder.c_str());

          cd_player_->pause();
          CdReader::AudioData data;
          char tmp_path[] = "/tmp/bambox-dump-XXXXXX";
          mkdtemp(tmp_path);
          spdlog::info("Dumping album too {}", tmp_path);
          for (const auto& song : current_cd_.songs_) {
            dump_disc_progress_slider_->init_async(100.0 * (song.track_num_ - 1.0) / current_cd_.songs_.size());
            cd_reader_->set_position(song.track_num_);

            std::string filename = fmt::format("{:02d} - {}.flac", song.track_num_, song.title_);
            std::string file_path = fmt::format("{}/{}", tmp_path, filename);
            FlacWriter writer(file_path, current_cd_, song.track_num_);
            while (1) {
              auto res = cd_reader_->read(data);
              if (res.is_error()) {
                spdlog::error("Failed to dump with {}", res.str());
                break;
              }
              if (data.frames == EOF) {
                break;
              }

              writer.write(data.data.data(), data.frames);
              if (data.ts != prev_sec) {
                prev_sec = data.ts;
                dump_song_progress_slider_->init_async(100.0 * data.ts.count() / (song.length_.count()));
              }
            }
            writer.finish();
            webDAV.upload_file(upload_folder + "/" + filename, file_path);
          }
          cd_reader_->set_position(0);
          cd_player_->play();
          spdlog::info("Dumping finished album: {}", tmp_path);

          std::filesystem::remove_all(tmp_path);
          auto cb = (GSourceOnceFunc) + [](BamBox* bambox) { bambox->ui_pop_stack(); };
          g_idle_add_once(cb, this);
        });
      }));

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

  // Done after UI as it will use gtk elements
  cd_player_->start();
}

void BamBox::ui_update_album_art() {
  auto cb = (GSourceOnceFunc)(+[](BamBox* bambox) {
    if (!bambox->current_cd_.album_art_path_.empty()) {
      gtk_image_set_from_file(bambox->album_art_, bambox->current_cd_.album_art_path_.c_str());
    } else {
      gtk_image_set_from_resource(bambox->album_art_, DEFAULT_IMAGE_PATH);
    }
  });

  g_idle_add_once(cb, this);
}

void BamBox::ui_update_track_info() {
  auto cb = (GSourceOnceFunc)(+[](BamBox* bambox) {
    std::string title = "";
    std::string artist = "";
    std::string album = "";

    if (bambox->current_cd_.songs_.size() > 0) {
      auto song = bambox->current_song_;
      auto cd = bambox->current_cd_;

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
    if (bambox->current_cd_.songs_.size() > 0) {
      auto song_length = bambox->current_song_.length_;
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
        if (cd_player_->pause().is_error()) {
          cd_player_->play();
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
