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

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "BamBoxError.hpp"

namespace bambox {

class CdReader {
 public:
  struct Song {
    std::chrono::seconds length_ = std::chrono::seconds(0);
    std::string title_;
    std::string artist_;
    uint64_t start_lba_ = 0;
    uint64_t end_lba_ = 0;
    uint8_t track_num_ = 0;
  };

  struct CD {
    std::string title_ = "Untitled";
    std::string artist_ = "Untitled";
    std::string genre_ = "Unknown";
    uint64_t sectors_;
    std::vector<Song> songs_ = {};
  };

  enum class State { STOPPED, PLAYING, STOPPING };

  /**
   * @brief callback used to send data from a track.
   *
   * @param std::chrono::seconds current progress in song
   * @param void*                Song data
   * @param int                  how many frames are contained in the data (note
   * this doesn't mean the length)
   */
  using SongDataCallback = std::function<int(std::chrono::seconds, void *, int)>;

 private:
  std::string mount_point_;
  CD current_cd_{};
  int handle_ = -1;
  std::mutex mtx_{};
  std::condition_variable cv_{};
  State state_ = State::STOPPED;

  // The LBA info of a track.
  // Only should be considered value when state_ is PLAYING or PAUSED
  uint32_t track_lba_start_ = 0;
  uint32_t track_lba_current_ = 0;
  uint32_t track_lba_end_ = 0;
  uint32_t track_num_ = 0;

  constexpr static int OP_RETRY_COUNT = 10;
  constexpr static std::chrono::seconds OP_RETRY_TIMEOUT = std::chrono::seconds(1);

 public:
  CdReader(const std::string &cd_dev);
  ~CdReader();

  Error eject();
  Error load();
  Error wait_for_disc();
  bool has_disc();

  Error play(const SongDataCallback &cb);
  Error set_position(uint8_t track_num, uint32_t lba_offset = 0);
  Error stop();

  const CD &get_disc() const { return current_cd_; }
  const Song &get_current_song() const {return current_cd_.songs_[track_num_-1];}

  // TODO maybe we should just provide them with an offset.
  uint32_t get_track_current_lba() const {
    return track_lba_current_;
  }
  uint32_t get_track_start_lba() const { return track_lba_start_; }
  uint32_t get_track_number() const { return track_num_; }

 private:
  /**
   * tries a function RetryCount times with a timeout inbetween before giving up
   * and returning return val.
   */
  Error retry_loader(std::function<Error(void)> func);
  Error do_eject();
  Error do_load();
  Error do_play(const SongDataCallback &cb);
};
}  // namespace bambox