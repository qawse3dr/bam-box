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
  while (1) {
    {
      std::lock_guard<std::mutex> lk(mtx_);
      state_ = State::NO_DISC;
    }
    spdlog::info("Waiting for CD...");
    ui_update_track_info();
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

    {
      std::lock_guard<std::mutex> lk(mtx_);
      state_ = State::PLAYING;
    }
    auto disc = cd_reader_->get_disc();
    spdlog::info("Playing cd: \"{}\" by: \"{}\"", disc.title_, disc.artist_);

    cd_reader_->set_position(1);
    ui_update_track_info();
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
        spdlog::info("next song");
        next();
      },
      std::chrono::milliseconds(500));

  gpio_->register_irq(
      cfg_.prev_gpio, {platform::Gpio::TriggerType::FALLING_EDGE},
      [&](unsigned int gpio, bool high) {
        spdlog::info("previous song");
        prev();
      },
      std::chrono::milliseconds(500));

  gpio_->register_irq(
      cfg_.play_gpio, {platform::Gpio::TriggerType::FALLING_EDGE},
      [&](unsigned int gpio, bool high) {
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
      },
      std::chrono::milliseconds(500));

  gpio_->register_irq(cfg_.rotary_encoder.button_gpio, {platform::Gpio::TriggerType::RISING_EDGE},
                      [&](unsigned int gpio, bool high) {
                        spdlog::info("Rotary Encoder pressed");

                        auto cb = (GSourceOnceFunc) + [](BamBox* bambox) {
                          gtk_widget_activate(GTK_WIDGET(bambox->buttons_[bambox->selected_button_idx_]));
                        };
                        g_idle_add_once(cb, this);
                      });

  gpio_->register_irq(
      cfg_.rotary_encoder.clk_gpio,
      {platform::Gpio::TriggerType::RISING_EDGE, platform::Gpio::TriggerType::FALLING_EDGE},
      [&](unsigned int gpio, bool high) {
        static bool old_state = false;

        if (high != old_state) {
          bool dt = gpio_->level_get(cfg_.rotary_encoder.data_gpio) != 0;
          if (dt) {  // Only do one of the bumps to avoid incrementing twice per turn.
            GSourceOnceFunc cb;
            if (high == dt) {
              cb = (GSourceOnceFunc) + [](BamBox* bambox) {
                if (bambox->selected_button_idx_ < (bambox->buttons_.size() - 1)) {
                  gtk_widget_unset_state_flags(GTK_WIDGET(bambox->buttons_[bambox->selected_button_idx_]),
                                               GTK_STATE_FLAG_PRELIGHT);
                  bambox->selected_button_idx_++;
                  gtk_widget_set_state_flags(GTK_WIDGET(bambox->buttons_[bambox->selected_button_idx_]),
                                             GTK_STATE_FLAG_PRELIGHT, FALSE);
                }
              };
            } else {
              cb = (GSourceOnceFunc) + [](BamBox* bambox) {
                if (bambox->selected_button_idx_ > 0) {
                  gtk_widget_unset_state_flags(GTK_WIDGET(bambox->buttons_[bambox->selected_button_idx_]),
                                               GTK_STATE_FLAG_PRELIGHT);
                  bambox->selected_button_idx_--;
                  gtk_widget_set_state_flags(GTK_WIDGET(bambox->buttons_[bambox->selected_button_idx_]),
                                             GTK_STATE_FLAG_PRELIGHT, FALSE);
                }
              };
            }
            g_idle_add_once(cb, this);
            // int vol = static_cast<int>(audio_player_->get_volume()) + ((high == dt) ? 1 : -1);
            // audio_player_->set_volume(std::max(vol, 0));
          }
        }
        old_state = high;
      });

  app_ = gtk_application_new("ca.larrycloud.bambox", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app_, "activate",
                   G_CALLBACK(+[](GtkApplication*, BamBox* bambox) -> void { bambox->ui_activate(); }), this);
  g_application_run(G_APPLICATION(app_), 0, nullptr);
  g_object_unref(G_APPLICATION(app_));

  // Start UI
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
  window_ = gtk_application_window_new(app_);
  gtk_window_set_title(GTK_WINDOW(window_), "Bam-Box");
  gtk_window_set_default_size(GTK_WINDOW(window_), 320, 240);
  gtk_window_fullscreen(GTK_WINDOW(window_));

  GtkCssProvider* provider = gtk_css_provider_new();
  GdkDisplay* display = gdk_display_get_default();
  gtk_css_provider_load_from_path(provider, "res/global.css");
  gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  GtkIconTheme* icon_theme = gtk_icon_theme_get_for_display(display);
  gtk_icon_theme_add_search_path(icon_theme, "res");

  auto* screen_overlays = gtk_overlay_new();
  gtk_window_set_child(GTK_WINDOW(window_), screen_overlays);

  // volume overlay
  volume_overlay_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_add_css_class(volume_overlay_, "volume-overlay-box");

  
  gtk_widget_set_halign(volume_overlay_, GTK_ALIGN_FILL);
  gtk_widget_set_valign(volume_overlay_, GTK_ALIGN_CENTER);
  gtk_widget_set_hexpand(volume_overlay_, true);
  gtk_widget_set_vexpand(volume_overlay_, false);
  volume_overlay_level_ = GTK_PROGRESS_BAR(gtk_progress_bar_new());
  gtk_widget_add_css_class(GTK_WIDGET(volume_overlay_level_), "volume-overlay");
  
  auto volume_overlay_text = gtk_label_new("Volume");
  gtk_widget_add_css_class(volume_overlay_text, "volume-overlay-text");

  gtk_box_append(GTK_BOX(volume_overlay_), volume_overlay_text);
  gtk_box_append(GTK_BOX(volume_overlay_), GTK_WIDGET(volume_overlay_level_));

  auto* vlayout_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_overlay_set_child(GTK_OVERLAY(screen_overlays), vlayout_box);
  gtk_overlay_add_overlay(GTK_OVERLAY(screen_overlays), volume_overlay_);
  gtk_widget_set_visible(volume_overlay_, false);

  song_progress_ = gtk_progress_bar_new();
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(song_progress_), 0);
  gtk_progress_bar_set_text(GTK_PROGRESS_BAR(song_progress_), "00:00/00:00");
  gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(song_progress_), true);
  gtk_widget_add_css_class(song_progress_, "song-progress");
  gtk_widget_set_halign(song_progress_, GTK_ALIGN_FILL);
  gtk_widget_set_valign(song_progress_, GTK_ALIGN_END);
  gtk_widget_set_hexpand(song_progress_, FALSE);
  gtk_widget_set_vexpand(song_progress_, TRUE);

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
  g_signal_connect(buttons_.back(), "clicked", G_CALLBACK(+[](GtkButton* button, BamBox* bambox) -> void {
                     spdlog::info("Volume onclick");
                     // Toggle Visible.
                     gtk_widget_set_visible(bambox->volume_overlay_, !gtk_widget_get_visible(bambox->volume_overlay_));
                   }),
                   this);
  buttons_.push_back(GTK_BUTTON(gtk_button_new_from_icon_name("headphone-symbolic")));
  buttons_.push_back(GTK_BUTTON(gtk_button_new_from_icon_name("song_list-symbolic")));
  buttons_.push_back(GTK_BUTTON(gtk_button_new_from_icon_name("settings-symbolic")));

  // Select the first button.
  gtk_widget_set_state_flags(GTK_WIDGET(buttons_.front()), GTK_STATE_FLAG_PRELIGHT, FALSE);
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

  gtk_window_present(GTK_WINDOW(window_));
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

    gtk_label_set_text(bambox->title_text_, title.c_str());
    gtk_label_set_text(bambox->artist_text_, artist.c_str());
    gtk_label_set_text(bambox->album_text_, album.c_str());
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

    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(bambox->song_progress_), track_percent);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(bambox->song_progress_), time_text.c_str());
  });
  g_idle_add_once(cb, this);
}