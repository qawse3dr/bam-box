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
#include "BamBoxConfig.hpp"

#include <getopt.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

using bambox::ECode;
using nlohmann::json;

void help_menu(std::ostream& out) {
  out << "Usage: bam-box --config <config>.json [OPTION]...\n"
      << "QNX8.0 based CD player\n"
      << "\n"
      << "  -h, --help                     Output this menu and exit.\n"
      << "  -c, --config       <cfg>       Configuration of bam-box.\n"
      << "  -l, --log-level    <level>     Log level must be one of (ERR, WARN, STAT, INFO, DBG).\n"
      << "  -f, --log-file     <file>      Logging file. If it already exists it will\n"
      << "                                 be appended to the existing log.\n"
      << "  -q, --quiet                    Won't log anything to the console and only the log file\n"
      << "                                 if provided." << std::endl;
}

static bambox::Error parse_config(bambox::BamBoxConfig& config, const char* config_path) {
  try {
    auto config_json = nlohmann::json::parse(std::ifstream(config_path), nullptr, true, true);
    config.cd_mount_point = config_json.at("cd_mount");
    config.cd_cache = config_json.at("cd_cache");
    config.dark_mode = config_json.at("dark_mode");
    config.default_audio_dev = config_json.at("audio_dev_default");
    config.webdav.url = config_json.at("webdav_url");
    config.webdav.user = config_json.at("webdav_username");
    config.webdav.pass = config_json.at("webdav_password");

    const auto& gpio = config_json.at("gpio");
    config.prev_gpio = gpio.at("prev");
    config.next_gpio = gpio.at("next");
    config.play_gpio = gpio.at("play");
    const auto& encoder = gpio.at("encoder");
    config.rotary_encoder.button_gpio = encoder.at("push");
    config.rotary_encoder.clk_gpio = encoder.at("clk");
    config.rotary_encoder.data_gpio = encoder.at("data");

    for (const auto& dev_json : config_json.at("audio_devs").items()) {
      config.audio_devs.push_back((bambox::AudioDevCfg){
          .display_name = dev_json.key(),
          .device_name = dev_json.value().at("dev"),
          .mixer_name = dev_json.value().at("mixer"),
          .volume = dev_json.value().at("volume"),
      });
    }

  } catch (const std::exception& e) {
    return {ECode::ERR_INVAL_ARG, fmt::format("Failed to json value with: {}", e.what())};
  }
  return {};
}

bambox::Expected<bambox::BamBoxConfig> parse_cli(int argc, char* argv[]) {
  bambox::BamBoxConfig cfg;
  int quiet_flag;
  const char* short_args = "qhl:f:c:";
  option long_options[] = {{.name = "config", .has_arg = required_argument, .flag = 0, .val = 'c'},
                           {.name = "log-level", .has_arg = required_argument, .flag = 0, .val = 'l'},
                           {.name = "log-file", .has_arg = required_argument, .flag = 0, .val = 'f'},
                           {.name = "quiet", .has_arg = required_argument, .flag = &quiet_flag, .val = 'q'},
                           {.name = "help", .has_arg = no_argument, .flag = 0, .val = 'h'},
                           {.name = nullptr, .has_arg = 0, .flag = nullptr, .val = 0}};

  int c;
  int option_index = 0;
  const char* config_path = nullptr;

  while ((c = getopt_long(argc, argv, short_args, long_options, &option_index)) != -1) {
    switch (c) {
      case 'h':
        help_menu(std::cout);
        exit(0);
        break;
      case 'c':
        config_path = optarg;
        break;
      case 'f':
        try {
          spdlog::default_logger()->sinks().emplace_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(optarg));
        } catch (const spdlog::spdlog_ex& e) {
        }
        break;
      case 'l': {
        spdlog::level::level_enum log_level;
        if (strcmp(optarg, "ERR") == 0) {
          log_level = spdlog::level::err;
        } else if (strcmp(optarg, "WARN") == 0) {
          log_level = spdlog::level::warn;
        } else if (strcmp(optarg, "STAT") == 0) {
          log_level = spdlog::level::info;
        } else if (strcmp(optarg, "INFO") == 0) {
          log_level = spdlog::level::info;
        } else if (strcmp(optarg, "DBG") == 0) {
          log_level = spdlog::level::debug;
        } else {
          return {ECode::ERR_INVAL_ARG, "Invalid log level must be one of (ERR, WARN, STAT, INFO, DBG)"};
        }
        spdlog::set_level(log_level);
        break;
      }
      case '?':
        help_menu(std::cerr);
        return {ECode::ERR_INVAL_ARG, fmt::format("Error on {}", argv[opterr])};
    }
  }

  if (config_path == nullptr) {
    return {ECode::ERR_NOFILE, "Missing config file"};
  }

  auto res = parse_config(cfg, config_path);
  if (res.is_error()) return res;
  cfg.config_path = config_path;

  return {cfg};
}

bambox::Error dump_config(const bambox::BamBoxConfig& cfg) {
  std::ofstream fp(cfg.config_path);
  if (!fp.is_open()) {
    return {ECode::ERR_IO, fmt::format("Failed to open path {} for dumping config", cfg.config_path)};
  }
  nlohmann::json cfg_json = nlohmann::json::object();
  cfg_json["cd_mount"] = cfg.cd_mount_point;
  cfg_json["cd_cache"] = cfg.cd_cache;
  cfg_json["dark_mode"] = cfg.dark_mode;
  cfg_json["webdav_url"] = cfg.webdav.url;
  cfg_json["webdav_username"] = cfg.webdav.user;
  cfg_json["webdav_password"] = cfg.webdav.pass;
  cfg_json["audio_dev_default"] = cfg.default_audio_dev;
  cfg_json["gpio"] = nlohmann::json::object();
  cfg_json["gpio"]["prev"] = cfg.prev_gpio;
  cfg_json["gpio"]["play"] = cfg.play_gpio;
  cfg_json["gpio"]["next"] = cfg.next_gpio;
  cfg_json["gpio"]["encoder"]["clk"] = cfg.rotary_encoder.clk_gpio;
  cfg_json["gpio"]["encoder"]["data"] = cfg.rotary_encoder.data_gpio;
  cfg_json["gpio"]["encoder"]["push"] = cfg.rotary_encoder.button_gpio;

  cfg_json["audio_devs"] = nlohmann::json::object();
  for (const auto& dev : cfg.audio_devs) {
    auto dev_json = nlohmann::json::object();
    dev_json["dev"] = dev.device_name;
    dev_json["mixer"] = dev.mixer_name;
    dev_json["volume"] = dev.volume;
    cfg_json["audio_devs"][dev.display_name] = dev_json;
  }

  fp << cfg_json.dump(4);
  return {};
}
