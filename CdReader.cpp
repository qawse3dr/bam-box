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
#include "CdReader.hpp"

#include <devctl.h>
#include <fcntl.h>
#include <libgen.h>
#include <sys/dcmd_cam.h>
#include <unistd.h>

#include <iostream>
#include <mutex>
#include <thread>

#include "BamBoxError.hpp"

using bambox::CdReader;

CdReader::CdReader(const std::string &cd_dev) : mount_point_(cd_dev) {}
CdReader::~CdReader() {
  if (handle_ != -1) {
    close(handle_);
  }
}

bambox::Error CdReader::load() { return retry_loader(std::bind(&CdReader::do_load, this)); }

static std::vector<std::string> cd_pkt_reader(char pkt_data[12]) {
  int i = 0;
  char data[13] = "d";
  std::vector<std::string> values;
  while (i < 12) {
    // If we ever find a blank record it means that it is Untitled so skip over
    // it and put a placeholder value.
    if (pkt_data[i] == '\0') {
      i++;
      values.push_back({});
      continue;
    }
    strlcpy(data, pkt_data + i, sizeof(data) - i);
    values.push_back(data);
    i += strlen(data) + 1;
  }
  return values;
}

bambox::Error CdReader::do_load() {
  CD cd;
  current_cd_ = cd;  // clear the cd

  if (handle_ != -1) {
    close(handle_);
  }

  handle_ = open(mount_point_.c_str(), O_RDONLY);
  if (handle_ < 0) {
    return {bambox::ECode::ERR_NOFILE, "Failed to open disc"};
  }

  devctl(handle_, DCMD_CAM_LOAD_MEDIA, NULL, 0, NULL);
  devctl(handle_, DCMD_CAM_CDROMSTART, NULL, 0, NULL);

  cam_devinfo_t info = {};
  int ret = devctl(handle_, DCMD_CAM_DEVINFO, &info, sizeof(info), NULL);
  if (ret != 0) {
    return {ECode::ERR_IO, "Failed to get devinfo from CD"};
  }

  // Read the length of the tracks so we know how to play each one
  cdrom_read_toc_t toc_data = {};
  ret = devctl(handle_, DCMD_CAM_CDROMREADTOC, &toc_data, sizeof(toc_data), NULL);
  if (ret != 0) {
    return {ECode::ERR_IO, "Failed to get TOC from CD"};
  }

  for (int i = toc_data.first_track; i <= toc_data.last_track; i++) {
    Song song;
    song.start_lba_ = toc_data.toc_entry[i - 1].addr.lba;
    song.track_num_ = toc_data.toc_entry[i - 1].track_number;
    if (!cd.songs_.empty()) {
      cd.songs_.back().end_lba_ = song.start_lba_ - 1;
    }
    cd.songs_.push_back(song);
  }

  // The last track ends at the last sector
  cd.songs_.back().end_lba_ = info.num_sctrs;  // maybe should be length?

  // Read the CD Text if it exists
  cdrom_cd_text_t cd_text = {};
  ret = devctl(handle_, DCMD_CAM_CDROM_TEXT, &cd_text, sizeof(cd_text), NULL);
  if (ret != 0) {
    return {ECode::ERR_IO, "Failed to get CD Text from CD"};
  }
  if (cd_text.npacks != 0) {
    cd.title_ = "";
    cd.artist_ = "";
  }
  for (int i = 0; i < cd_text.npacks; i++) {
    cdrom_datapack_t pkt = cd_text.packs[i];

    switch (pkt.pack_type) {
      case CDROM_DPT_TITLE: {
        auto pkts = cd_pkt_reader(pkt.data);

        for (const auto &pkt_val : pkts) {
          std::string &title_dest = (pkt.trk == 0) ? cd.title_ : cd.songs_[pkt.trk - 1].title_;
          title_dest += pkt_val;
          pkt.trk++;
        }
        break;
      }
      case CDROM_DPT_PERFORMER: {
        auto pkts = cd_pkt_reader(pkt.data);

        for (const auto &pkt_val : pkts) {
          std::string &artist_dest = (pkt.trk == 0) ? cd.artist_ : cd.songs_[pkt.trk - 1].artist_;
          artist_dest += pkt_val;
          pkt.trk++;
        }
        break;
      }
      default:
        std::cout << "Unknown cdtext " << static_cast<int>(pkt.pack_type) << std::endl;
        break;
    }
  }

  if (cd.artist_.empty()) {
    cd.artist_ = cd.songs_[0].artist_;
  }
  if (cd.title_.empty()) {
    cd.title_ = "Untilted";
  }

  for (Song &song : cd.songs_) {
    size_t i = song.title_.find(" - ");
    if (i != std::string::npos && song.artist_.empty()) {
      // likely they put the artist with the song title...
      song.artist_ = song.title_.substr(i + 3);
      song.title_ = song.title_.substr(0, i);
    }

    // Calculate length
    song.length_ = std::chrono::seconds(LBA2SEC(song.end_lba_ - song.start_lba_)) +
                   std::chrono::minutes(LBA2MIN(song.end_lba_ - song.start_lba_));
  }

  current_cd_ = cd;
  set_position(1);
  return {};
}

bambox::Error CdReader::set_position(uint8_t track_num, uint32_t lba_offset) {
  std::lock_guard<std::mutex> lk(mtx_);
  if (track_num > current_cd_.songs_.size()) {
    return {ECode::ERR_RANGE, "Seek out of range for cd"};
  }
  track_lba_start_ = current_cd_.songs_[track_num - 1].start_lba_;
  track_lba_current_ = current_cd_.songs_[track_num - 1].start_lba_ + lba_offset;
  track_lba_end_ = current_cd_.songs_[track_num - 1].end_lba_;
  track_num_ = track_num;
  return {};
}

bambox::Error CdReader::stop() {
  std::unique_lock<std::mutex> lk(mtx_);
  if (state_ != State::PLAYING) {
    std::cout << "failed to stop" << std::endl;
    return {ECode::ERR_AGAIN, "Failed to stop not running"};
  }

  std::cout << "stopping" << std::endl;

  state_ = State::STOPPING;
  cv_.wait(lk, [&]() { return state_ == State::STOPPED; });
  return {};
}

bambox::Error CdReader::eject() { return retry_loader(std::bind(&CdReader::do_eject, this)); }

bambox::Error CdReader::do_eject() {
  auto ret = devctl(handle_, DCMD_CAM_EJECT_MEDIA, NULL, 0, NULL);
  if (ret == -1) {
    return {ECode::ERR_UNKNOWN, "Failed to eject disc."};
  }
  return {};
}

bambox::Error CdReader::play(const SongDataCallback &cb) {
  return retry_loader(std::bind(&CdReader::do_play, this, cb));
}

#define READ_SIZE CDROM_CDDA_FRAME_SIZE
typedef union {
  cdrom_raw_read_t read;
  uint8_t data[READ_SIZE];
} raw_read_reqest_t;

bambox::Error CdReader::do_play(const SongDataCallback &cb) {
  std::unique_lock<std::mutex> lk(mtx_);
  state_ = State::PLAYING;
  // Set the current tracks info.
  track_lba_start_ = current_cd_.songs_[track_num_ - 1].start_lba_;
  track_lba_end_ = current_cd_.songs_[track_num_ - 1].end_lba_;

  for (; track_lba_current_ < track_lba_end_; track_lba_current_ += 1) {
    raw_read_reqest_t req = {.read = {.lba = track_lba_current_, .nsectors = 1, .est = CDROM_EST_CDDA}};

    lk.unlock();
    int ret = devctl(handle_, DCMD_CAM_CDROMREAD, &req, sizeof(req), NULL);
    if (ret != 0) {
      return {bambox::ECode::ERR_IO, "Failed to read cd"};
    }

    // TODO(qawse3dr) check callback result.
    cb(std::chrono::minutes(LBA2MIN(track_lba_current_ - track_lba_start_)) + std::chrono::seconds(LBA2SEC(track_lba_current_ - track_lba_start_)), req.data,
       CDROM_CDDA_FRAME_SIZE / 4);

    lk.lock();
    if (state_ != State::PLAYING) {
      break;
    }
  }

  state_ = State::STOPPED;
  cv_.notify_all();

  return {};
}

bambox::Error CdReader::retry_loader(std::function<bambox::Error(void)> func) {
  bambox::Error res(ECode::ERR_UNKNOWN, "");
  for (int i = 0; i < OP_RETRY_COUNT && res.is_error(); i++) {
    // If the function failed reload the cd and cdinfo.
    res = func();
    if (res.is_error() && i != OP_RETRY_COUNT - 1) {
      // No file means cd was removed.

      auto load_res = do_load();
      if (load_res.code == bambox::ECode::ERR_NOFILE) {
        return load_res;
      }
      std::this_thread::sleep_for(OP_RETRY_TIMEOUT);
    }
  }

  return res;
}

bambox::Error CdReader::wait_for_disc() {
  if (waitfor_attach(mount_point_.c_str(), 1000) == EOK) {
    return {};
  }
  return {bambox::ECode::ERR_TIMEOUT, "no disc"};
}
bool CdReader::has_disc() { return access(mount_point_.c_str(), R_OK); }
