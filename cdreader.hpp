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

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

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

  enum class State { UNKNOWN, EJECTED, LOADING, PLAYING, PAUSED, NO_DISC };

  /**
   * @brief callback used to send data from a track.
   *
   * @param std::chrono::seconds current progress in song
   * @param void*                Song data
   * @param int                  how many frames are contained in the data (note
   * this doesn't mean the length)
   */
  using SongDataCallback =
      std::function<int(std::chrono::seconds, void *, int)>;

private:
  std::string mount_point_;
  CD current_cd_{};
  int handle_ = -1;
  std::mutex mtx_{};
  std::condition_variable cv_{};
  State state_ = State::UNKNOWN;

  // The LBA info of a track.
  // Only should be considered value when state_ is PLAYING or PAUSED
  uint32_t track_lba_start_ = 0;
  uint32_t track_lba_current_ = 0;
  uint32_t track_lba_end_ = 0;
  uint32_t track_num_ = 0;

  constexpr static int OP_RETRY_COUNT = 10;
  constexpr static std::chrono::seconds OP_RETRY_TIMEOUT =
      std::chrono::seconds(1);

public:
  CdReader(const std::string &cd_dev);
  ~CdReader();

  int eject();
  int load();
  int play_track(uint8_t track_num, const SongDataCallback &cb);

  const CD &get_disc() const { return current_cd_; }
  State get_state();

  /// Controls for CD
  int pause();
  int resume();

  // TODO These should really be control from the CD caller not the cd reader
  // itself. it should instead expose an API which stops the current playing as
  // well as provide an api to see how far in a song we were.
  int prev();
  int next();

private:
  /**
   * tries a function RetryCount times with a timeout inbetween before giving up
   * and returning return val.
   */
  int retry_loader(std::function<int(void)> func);
  int do_eject();
  int do_load();
  int do_play_track(uint8_t track_num, const SongDataCallback &cb);
};
} // namespace bambox