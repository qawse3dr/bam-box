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

#include <algorithm>
#include <fstream>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <sstream>
#include <thread>

#include "BamBoxError.hpp"

using bambox::CdReader;

/// Sectors in a second of CD audio.
#define SECTORS_PER_SEC 75

/// Lead-out + lead-in burned between the audio session and a following data
/// session on an enhanced CD.
#define SESSION_GAP_SECTORS 11400

CdReader::CdReader(const bambox::BamBoxConfig &cfg) : cfg_(cfg), mount_point_(cfg.cd_mount_point) {}
CdReader::~CdReader() {
  if (handle_ != -1) {
    close(handle_);
  }
}

static std::vector<std::string> cd_pkt_reader(const char pkt_data[CDROM_DATA_SIZE]) {
  int i = 0;
  std::vector<std::string> values;
  while (i < CDROM_DATA_SIZE) {
    // If we ever find a blank record it means that it is Untitled so skip over
    // it and put a placeholder value.
    if (pkt_data[i] == '\0') {
      i++;
      values.push_back({});
      continue;
    }

    // The pack payload is a fixed 12 byte field and is not guaranteed to be
    // terminated, so never walk past the end of it looking for a NUL.
    values.push_back("");
    while (i < CDROM_DATA_SIZE && pkt_data[i] != '\0') {
      if (isprint(static_cast<unsigned char>(pkt_data[i]))) {
        values.back() += pkt_data[i];
      }
      i++;
    }
    i++;  // step over the terminator (or past the end, ending the loop)
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

  if (toc_data.last_track < toc_data.first_track || toc_data.last_track >= CDROM_MAX_TRACKS) {
    return {ECode::ERR_IO, fmt::format("CD reported a bogus TOC (first={} last={})", toc_data.first_track,
                                       toc_data.last_track)};
  }
  const int ntracks = toc_data.last_track - toc_data.first_track + 1;

  // The driver stores the lead-out descriptor immediately after the last track.
  const uint64_t leadout_lba = toc_data.toc_entry[ntracks].addr.lba;

  for (int i = 0; i < ntracks; i++) {
    const cdrom_tocentry_t &entry = toc_data.toc_entry[i];
    const uint8_t adr = (entry.control_adr >> 4) & 0x0F;
    const uint8_t control = entry.control_adr & 0x0F;
    const uint64_t start_lba = entry.addr.lba;
    const bool is_data = (control & CDROM_DATA_TRACK) != 0;  // bit 2 marks a data track

    // Every descriptor (audio or data) bounds the previous audio track. A data track
    // belongs to a later session, so the audio ends a session gap before it starts.
    if (!cd.songs_.empty() && cd.songs_.back().end_lba_ == 0) {
      uint64_t boundary = start_lba;
      if (is_data && boundary > SESSION_GAP_SECTORS) {
        boundary -= SESSION_GAP_SECTORS;
      }
      cd.songs_.back().end_lba_ = boundary - 1;
    }

    if (is_data) {
      spdlog::info("data_track {} toc= {} control={}, addr={}... skipping", entry.track_number, start_lba, control,
                   adr);
      continue;
    }

    Song song;
    song.start_lba_ = start_lba;
    song.track_num_ = entry.track_number;
    cd.songs_.push_back(song);

    spdlog::info("track {} toc= {} control={}, addr={}", entry.track_number, start_lba, control, adr);
  }

  if (cd.songs_.empty()) {
    return {ECode::ERR_NO_DATA, "No audio tracks on this disc"};
  }

  // Nothing followed the last audio track, so it runs up to the lead-out.
  if (cd.songs_.back().end_lba_ == 0) {
    cd.songs_.back().end_lba_ = leadout_lba - 1;
  }
  // The audio session ends one sector after the last audio track; that is the
  // lead-out MusicBrainz/freedb want when hashing the disc.
  cd.lout_track_lba_ = cd.songs_.back().end_lba_ + 1;
  spdlog::info("track lout {}", cd.lout_track_lba_);

  // Read the CD Text if it exists
  cdrom_cd_text_t cd_text = {};
  ret = devctl(handle_, DCMD_CAM_CDROM_TEXT, &cd_text, sizeof(cd_text), NULL);
  if (ret != 0) {
    return {ECode::ERR_IO, "Failed to get CD Text from CD"};
  }
  // npacks comes off the disc, don't trust it to be within the array.
  const int npacks = std::min<int>(cd_text.npacks, CDROM_MAX_TEXT);
  if (npacks != 0) {
    cd.title_ = "";
    cd.artist_ = "";
  }
  for (int i = 0; i < npacks; i++) {
    cdrom_datapack_t pkt = cd_text.packs[i];

    switch (pkt.pack_type) {
      case CDROM_DPT_TITLE: {
        auto pkts = cd_pkt_reader(pkt.data);

        for (const auto &pkt_val : pkts) {
          // pkt.trk is a track number off the disc, it may not be an audio track.
          int idx = cd.index_of_track(pkt.trk);
          if (pkt.trk == 0) {
            cd.title_ += pkt_val;
          } else if (idx >= 0) {
            cd.songs_[idx].title_ += pkt_val;
          }
          pkt.trk++;
        }
        break;
      }
      case CDROM_DPT_PERFORMER: {
        auto pkts = cd_pkt_reader(pkt.data);

        for (const auto &pkt_val : pkts) {
          int idx = cd.index_of_track(pkt.trk);
          if (pkt.trk == 0) {
            cd.artist_ += pkt_val;
          } else if (idx >= 0) {
            cd.songs_[idx].artist_ += pkt_val;
          }
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

    // Calculate length. LBA2SEC/LBA2MIN convert an *absolute* address and fold in
    // the 150 sector pregap, so they can't be used on a difference.
    song.length_ = std::chrono::seconds((song.end_lba_ - song.start_lba_ + 1) / SECTORS_PER_SEC);
  }
  current_cd_ = cd;
  update_disc_info();
  return set_position(current_cd_.songs_.front().track_num_);
}

bambox::Error CdReader::set_position(uint8_t track_num, uint32_t lba_offset) {
  // songs_ holds audio tracks only, so the track number isn't an index into it.
  int idx = current_cd_.index_of_track(track_num);
  if (idx < 0) {
    return {ECode::ERR_RANGE, "Seek out of range for cd"};
  }
  track_lba_start_ = current_cd_.songs_[idx].start_lba_;
  track_lba_current_ = current_cd_.songs_[idx].start_lba_ + lba_offset;
  track_lba_end_ = current_cd_.songs_[idx].end_lba_;
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

  // end of track return EOF. end_lba_ is the last sector of the track, and this
  // has to be >= so a seek that lands past the end still terminates.
  if (track_lba_current_ > track_lba_end_) {
    audio.frames = EOF;
    return {};
  }

  raw_read_request_t req = {.read = {.lba = track_lba_current_, .nsectors = 1, .est = CDROM_EST_CDDA}};
  int ret = devctl(handle_, DCMD_CAM_CDROMREAD, &req, sizeof(req), NULL);
  if (ret != 0) {
    return {bambox::ECode::ERR_IO, "Failed to read CD", ret};
  }

  // Elapsed time is a sector *difference*, so the absolute-address LBA2* macros
  // (which add the 150 sector pregap) can't be used here.
  audio.ts = std::chrono::seconds((track_lba_current_ - track_lba_start_) / SECTORS_PER_SEC);
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

  // discid indexes offsets by CD track number, not by position in songs_, so the
  // array has to be sized for the highest track number on the disc.
  std::vector<int> offsets(cd.songs_.back().track_num_ + 1, 0);

  offsets[0] = cd.lout_track_lba_ + 150;
  spdlog::info("0={}", cd.lout_track_lba_ + 150);

  for (auto &song : cd.songs_) {
    spdlog::info("{}={}", song.track_num_, song.start_lba_);
    offsets[song.track_num_] = song.start_lba_ + 150;
  }
  bool success = discid_put(disc, cd.songs_.front().track_num_, cd.songs_.back().track_num_, offsets.data());
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

  // Nothing else works if the cache dir isn't there.
  std::error_code ec;
  std::filesystem::create_directories(cfg_.cd_cache, ec);

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
    fp.close();
    spdlog::info("update_disc_info discid={} curl_res={}", disc_id, curl_easy_strerror(curl_res));

    // Don't leave a partial or error response behind to be cached forever.
    if (curl_res != CURLE_OK) {
      unlink(cached_path.c_str());
      return {ECode::ERR_IO, fmt::format("Failed to fetch disc info: {}", curl_easy_strerror(curl_res))};
    }
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
        // "position" counts every track on the medium, data tracks included, so it
        // is a CD track number and not an index into songs_.
        int track_num = track["position"];
        int idx = current_cd_.index_of_track(track_num);
        if (idx < 0) {
          continue;
        }
        current_cd_.songs_[idx].artist_ = current_cd_.artist_;
        current_cd_.songs_[idx].title_ = track["title"];
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
        if (fp == NULL) {
          spdlog::warn("Failed to open {} for writing: {}", current_cd_.album_art_path_, strerror(errno));
          break;
        }
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

      // A partial download would otherwise be cached (and shown) forever.
      if (curl_res != CURLE_OK) {
        unlink(current_cd_.album_art_path_.c_str());
        current_cd_.album_art_path_.clear();
      }
    }
  }

  return {};
}
