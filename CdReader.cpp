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

#include <curl/curl.h>
#include <devctl.h>
#include <discid/discid.h>
#include <fcntl.h>
#include <libgen.h>
#include <spdlog/spdlog.h>
#include <sys/dcmd_cam.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <sstream>
#include <thread>

#include "BamBoxError.hpp"

using bambox::CdReader;

CdReader::CdReader(const bambox::BamBoxConfig &cfg) : cfg_(cfg), mount_point_(cfg.cd_mount_point) {}
CdReader::~CdReader() {
  if (handle_ != -1) {
    close(handle_);
  }
}

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
    values.push_back("");
    for (char *s = data; *s != '\0'; s++) {
      if (isprint(*s)) {
        values.back() += *s;
      }
    }
    i += strlen(data) + 1;
  }
  return values;
}

bambox::Error CdReader::load() {
  CD cd;
  current_cd_ = cd;  // clear the cd

  if (handle_ != -1) {
    close(handle_);
    handle_ = -1;
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
    return {ECode::ERR_NOFILE, "Failed to get devinfo from CD"};
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

    spdlog::info("track {} toc= {}", i, song.start_lba_);
  }

  // The last track ends at the last sector
  cd.songs_.back().end_lba_ = info.num_sctrs;
  cd.lout_track_lba_ = toc_data.toc_entry[toc_data.last_track].addr.lba;

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
    cd.title_ = "Untitled";
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
  update_disc_info();
  set_position(1);
  return {};
}

bambox::Error CdReader::set_position(uint8_t track_num, uint32_t lba_offset) {
  if (track_num > current_cd_.songs_.size() || track_num == 0) {
    return {ECode::ERR_RANGE, "Seek out of range for cd"};
  }
  track_lba_start_ = current_cd_.songs_[track_num - 1].start_lba_;
  track_lba_current_ = current_cd_.songs_[track_num - 1].start_lba_ + lba_offset;
  track_lba_end_ = current_cd_.songs_[track_num - 1].end_lba_;
  track_num_ = track_num;
  return {};
}

bambox::Error CdReader::eject() {
  if (handle_ == -1) {
    return {ECode::ERR_NOFILE, "Disc not loaded"};
  }

  auto ret = devctl(handle_, DCMD_CAM_EJECT_MEDIA, NULL, 0, NULL);
  if (ret == -1) {
    return {ECode::ERR_UNKNOWN, "Failed to eject disc."};
  }

  CD cd;
  current_cd_ = cd;  // clear the cd

  if (handle_ != -1) {
    close(handle_);
    handle_ = -1;
  }

  return {};
}

#define READ_SIZE CDROM_CDDA_FRAME_SIZE
typedef union {
  cdrom_raw_read_t read;
  uint8_t data[READ_SIZE];
} raw_read_request_t;

bambox::Error CdReader::read(CdReader::AudioData &audio) {
  if (handle_ == -1) {
    return {ECode::ERR_NOFILE, "Disc not loaded"};
  }

  // end of track return EOF
  if (track_lba_current_ == track_lba_end_) {
    audio.frames = EOF;
    return {};
  }

  raw_read_request_t req = {.read = {.lba = track_lba_current_, .nsectors = 1, .est = CDROM_EST_CDDA}};
  int ret = devctl(handle_, DCMD_CAM_CDROMREAD, &req, sizeof(req), NULL);
  if (ret != 0) {
    return {bambox::ECode::ERR_IO, "Failed to read CD", ret};
  }

  audio.ts = std::chrono::minutes(LBA2MIN(track_lba_current_ - track_lba_start_)) +
             std::chrono::seconds(LBA2SEC(track_lba_current_ - track_lba_start_));
  memcpy(audio.data.data(), req.data, sizeof(req.data));
  audio.frames = CDROM_CDDA_FRAME_SIZE / 4;
  track_lba_current_++;
  return {};
}

bambox::Error CdReader::wait_for_disc() {
  if (waitfor_attach(mount_point_.c_str(), 1000) == EOK) {
    return {};
  }
  return {bambox::ECode::ERR_TIMEOUT, "no disc"};
}
bool CdReader::has_disc() { return 0 == access(mount_point_.c_str(), R_OK); }

static DiscId *create_disc_id_from_toc(const bambox::CdReader::CD &cd) {
  if (cd.songs_.size() == 0) {
    return NULL;
  }
  DiscId *disc = discid_new();
  int offsets[cd.songs_.size() + 1];

  offsets[0] = cd.lout_track_lba_ + 150;
  for (auto &song : cd.songs_) {
    offsets[song.track_num_] = song.start_lba_ + 150;
  }
  bool success = discid_put(disc, cd.songs_.front().track_num_, cd.songs_.back().track_num_, offsets);
  if (!success) {
    spdlog::warn("Failed to get disc id with: {}", discid_get_error_msg(disc));
    discid_free(disc);
    return NULL;
  }

  return disc;
}

std::string CdReader::get_disc_id(const CD &cd) {
  DiscId *disc = create_disc_id_from_toc(cd);
  if (disc == NULL) {
    return "";
  }
  std::string id = discid_get_id(disc);
  discid_free(disc);
  return id;
}

std::string CdReader::get_freedb_id(const CD &cd) {
  DiscId *disc = create_disc_id_from_toc(cd);
  if (disc == NULL) {
    return "";
  }

  std::string id = discid_get_freedb_id(disc);
  discid_free(disc);
  return id;
}

bambox::Error CdReader::update_disc_info() {
  if (handle_ < 0) {
    return {ECode::ERR_INVAL_STATE, "CD not loaded can't pull cd info."};
  }

  // TODO move this to a separate thread
  std::string json_val = "";
  auto disc_id = get_disc_id(current_cd_);
  auto cached_path = cfg_.cd_cache + "/" + disc_id + ".json";
  if (std::filesystem::exists(cached_path)) {
    // Info already cached TODO(qawse3dr) we probably want a sqlite3 server for this instead of saving all
    // the json as it will take up a bunch of space we really don't need it to.
    spdlog::info("info for discid={} already cached", disc_id);
  } else {  // fetch from the interwebs
    auto discid_url_write_ftn = +[](char *ptr, size_t size, size_t nmemb, void *userdata) -> size_t {
      (reinterpret_cast<std::ofstream *>(userdata))->write(ptr, nmemb);
      return nmemb;
    };

    std::ofstream fp(cached_path);
    CURL *curl = curl_easy_init();
    std::string discid_url = "http://musicbrainz.org/ws/2/discid/" + disc_id + "?inc=recordings+artists&fmt=json";
    curl_easy_setopt(curl, CURLOPT_URL, discid_url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "bambox/0.1 (lawrencemilne38@gmail.com)");
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &fp);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discid_url_write_ftn);
    CURLcode curl_res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    spdlog::info("update_disc_info discid curl_res={}", curl_easy_strerror(curl_res));
  }

  try {
    std::ifstream fp(cached_path);
    auto discid_body = nlohmann::json::parse(fp);
    for (auto release : discid_body["releases"]) {
      if (release.contains("artist-credit") && release["artist-credit"].size() > 0) {
        current_cd_.artist_ = release["artist-credit"].front()["name"];
      }
      if (release.contains("date")) {
        current_cd_.release_date_ = release["date"];
      }

      current_cd_.title_ = release["title"];
      for (auto track : release["media"][0]["tracks"]) {
        // TODO update to get it from the track info instead.
        int track_num = track["position"];
        current_cd_.songs_[track_num - 1].artist_ = current_cd_.artist_;
        current_cd_.songs_[track_num - 1].title_ = track["title"];
      }
      if (release["cover-art-archive"]["front"] == true) {
        current_cd_.release_id_ = release["id"];
        break;
      }
    }
  } catch (const std::exception &e) {
    // remove the file on failure
    unlink(cached_path.c_str());
    return {ECode::ERR_IO, "Failed to parse json with" + std::string(e.what())};
  }

  // Pull album art if it doesn't exist
  if (!current_cd_.release_id_.empty()) {
    current_cd_.album_art_path_ = cfg_.cd_cache + "/" + current_cd_.release_id_ + ".jpg";
    if (!std::filesystem::exists(current_cd_.album_art_path_)) {
      CURLcode curl_res = CURLE_AGAIN;
      std::string album_art_url = "http://coverartarchive.org/release/" + current_cd_.release_id_ + "/front-250";
      spdlog::info("Fetching album art from {}", album_art_url);
      for (int i = 0; i < 3 && curl_res != CURLE_OK; i++) {
        FILE *fp = fopen(current_cd_.album_art_path_.c_str(), "wb");
        CURL *curl = curl_easy_init();
        curl_easy_setopt(curl, CURLOPT_URL, album_art_url.c_str());
        curl_easy_setopt(curl, CURLOPT_FILE, fp);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_res = curl_easy_perform(curl);
        spdlog::info("update_disc_info art curl_res={}", curl_easy_strerror(curl_res));
        curl_easy_cleanup(curl);
        fclose(fp);
      }
    }
  }

  return {};
}
