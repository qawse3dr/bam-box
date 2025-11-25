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
#include "cdreader.hpp"

#include <devctl.h>
#include <fcntl.h>
#include <sys/dcmd_cam.h>

#include <iostream>
#include <thread>

using bambox::CdReader;

CdReader::CdReader(const std::string &cd_dev) : mount_point_(cd_dev) {}
CdReader::~CdReader() {
  if (handle_ != -1) {
    close(handle_);
  }
}

int CdReader::load() {
  return retry_loader(std::bind(&CdReader::do_load, this));
}

static std::vector<std::string> cd_pkt_reader(char pkt_data[12]) {
  int i = 0;
  char data[13] = "d";
  std::vector<std::string> values;
  while (i < 12 && data[0] != '\0') {

    strlcpy(data, pkt_data + i, sizeof(data) - i);
    values.push_back(data);
    i += strlen(data) + 1;
  }
  return values;
}

int CdReader::do_load() {
  CD cd;
  current_cd_ = cd; // clear the cd

  if (handle_ != -1) {
    close(handle_);
  }

  handle_ = open(mount_point_.c_str(), O_RDONLY);
  if (handle_ < 0) {
    return 1;
  }

  devctl(handle_, DCMD_CAM_LOAD_MEDIA, NULL, 0, NULL);
  devctl(handle_, DCMD_CAM_CDROMSTART, NULL, 0, NULL);

  cam_devinfo_t info = {};
  int ret = devctl(handle_, DCMD_CAM_DEVINFO, &info, sizeof(info), NULL);
  if (ret != 0) {
    return ret;
  }

  // Read the length of the tracks so we know how to play each one
  cdrom_read_toc_t toc_data = {};
  ret =
      devctl(handle_, DCMD_CAM_CDROMREADTOC, &toc_data, sizeof(toc_data), NULL);
  if (ret != 0) {
    return ret;
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
    return ret;
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
        std::string &title_dest =
            (pkt.trk == 0) ? cd.title_ : cd.songs_[pkt.trk - 1].title_;
        title_dest += pkt_val;
        pkt.trk++;
      }
      break;
    }
    case CDROM_DPT_PERFORMER: {
      auto pkts = cd_pkt_reader(pkt.data);

      for (const auto &pkt_val : pkts) {
        std::string &artist_dest =
            (pkt.trk == 0) ? cd.artist_ : cd.songs_[pkt.trk - 1].artist_;
        artist_dest += pkt_val;
        pkt.trk++;
      }
      break;
    }
    }
  }

  current_cd_ = cd;
  return 0;
}

int CdReader::eject() {
  return retry_loader(std::bind(&CdReader::do_eject, this));
}

int CdReader::do_eject() {
  return devctl(handle_, DCMD_CAM_EJECT_MEDIA, NULL, 0, NULL);
}

int CdReader::play_track(uint8_t track_num, const SongDataCallback &cb) {
  return retry_loader(std::bind(&CdReader::do_play_track, this, track_num, cb));
}

#define READ_SIZE CDROM_CDDA_FRAME_SIZE
typedef union {
  cdrom_raw_read_t read;
  uint8_t data[READ_SIZE];
} raw_read_reqest_t;

int CdReader::do_play_track(uint8_t track_num, const SongDataCallback &cb) {
  unsigned int lba = current_cd_.songs_[track_num - 1].start_lba_;
  unsigned int lba_end = current_cd_.songs_[track_num - 1].end_lba_;
  for (; lba < lba_end; lba += 1) {
    raw_read_reqest_t req = {
        .read = {.lba = lba, .nsectors = 1, .est = CDROM_EST_CDDA}};
    int ret = devctl(handle_, DCMD_CAM_CDROMREAD, &req, sizeof(req), NULL);
    if (ret != 0) {
      return -1; // TODO be able to skip sectors and hold space
    }
    cb(std::chrono::minutes(LBA2MIN(lba)) + std::chrono::seconds(lba), req.data,
       CDROM_CDDA_FRAME_SIZE / 4);
  }

  return 0;
}

int CdReader::retry_loader(std::function<int(void)> func) {
  int ret = -1;
  for (int i = 0; i < OP_RETRY_COUNT && ret != 0; i++) {
    ret = func();

    // If the function failed reload the cd and cdinfo.
    if (ret != 0 && i != OP_RETRY_COUNT - 1) {
      do_load();
      std::this_thread::sleep_for(OP_RETRY_TIMEOUT);
    }
  }

  return ret;
}
