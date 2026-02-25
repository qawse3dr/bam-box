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

#include <memory>
#include <thread>
#include <variant>

#include "AudioPlayer.hpp"
#include "CdReader.hpp"
#include "BamBoxError.hpp"

namespace bambox {

/**
 * Plays music from the CdReader to the AudioPlayer to a file.
 */
class CdPlayer {
 private:
  enum class State {PLAYING, STOPPING, STOPPED};
 public:
  enum class Event {
    STARTUP,         // No data
    CD_EJECTED,      // Error
    CD_TRACK_ENDED,  // no data
    CD_LOADED,       // CDReader::CD
    CD_TIME_UPDATE,  // std::chrono::seconds
  };
  using EventData = std::variant<std::chrono::seconds, std::string, int64_t, CdReader::CD, Error>;
  using EventCB = std::function<void(Event event, const EventData& data)>;

 public:
  CdPlayer(const std::shared_ptr<CdReader>& reader, std::shared_ptr<AudioPlayer>& audio_player, EventCB event_cb);


  Error start();
  Error load();
  Error play();
  Error pause();

 private:
  void cd_reader_loop();
  void cd_loader_loop();

 private:
  std::shared_ptr<CdReader> reader_{};
  std::shared_ptr<AudioPlayer> audio_player_{};
  EventCB event_cb_{};

  std::thread cd_reader_loop_;
  std::thread cd_loader_loop_;
  std::mutex mtx_;
  State state_ = State::STOPPED;

  bool has_disc_ = false;
};
}  // namespace bambox
