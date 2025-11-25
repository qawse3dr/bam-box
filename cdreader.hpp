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
#include <functional>
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
  int handle_ = -1;
  CD current_cd_;

  constexpr static int OP_RETRY_COUNT = 10;
  constexpr static std::chrono::seconds OP_RETRY_TIMEOUT =
      std::chrono::seconds(1);

public:
  CdReader(const std::string &cd_dev);
  ~CdReader();

  int eject();
  int load();
  int play_track(uint8_t track_num, const SongDataCallback &cb);

  const CD &get_disc() { return current_cd_; }

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