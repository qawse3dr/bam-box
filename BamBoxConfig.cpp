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

#include <iostream>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
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

  // parse config


  return {cfg};
}
