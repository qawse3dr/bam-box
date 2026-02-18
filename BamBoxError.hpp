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

#include <string>
#include <spdlog/fmt/fmt.h>

namespace bambox {

enum class ECode {
  ERR_OK = 0,
  ERR_UNKNOWN,
  ERR_INVAL_STATE,
  ERR_AGAIN,
  ERR_INVAL_ARG,
  ERR_TIMEOUT,
  ERR_NOFILE,
  ERR_IO,
  ERR_RANGE,
  ERR_OOM
};

struct Error {
  Error(ECode err_code, const std::string &err_msg) : code(err_code), msg(err_msg) {}
  Error() = default;
  ECode code = ECode::ERR_OK;
  std::string msg = "";

  inline bool is_error() { return code != ECode::ERR_OK; }
  inline bool is_ok() { return code == ECode::ERR_OK; }

  std::string str() { return fmt::format("{}: {}", ecode_as_str(code), msg); }
  inline const char *ecode_as_str(ECode code) {
    switch (code) {
      case ECode::ERR_OK:
        return "ERROR_OK";
      case ECode::ERR_UNKNOWN:
        return "ERROR_UNKNOWN";
      case ECode::ERR_INVAL_STATE:
        return "ERROR_INVALID_STATE";
      case ECode::ERR_AGAIN:
        return "ERROR_AGAIN";
      case ECode::ERR_INVAL_ARG:
        return "ERROR_INVALID_ARGUMENT";
      case ECode::ERR_TIMEOUT:
        return "ERROR_TIMEOUT";
      case ECode::ERR_NOFILE:
        return "ERROR_NO_FILE";
      case ECode::ERR_IO:
        return "ERROR_IO";
      case ECode::ERR_RANGE:
        return "ERROR_RANGE";
      case ECode::ERR_OOM:
        return "ERROR_OOM";
      default:
        return "ERROR_UNKNOWN";
    }
  }
};

template <typename T>
struct Expected : public Error {
  Expected() = default;
  Expected(const T &exp_val) : val(exp_val) {}
  Expected(T &&exp_val) : val(std::move(exp_val)) {}
  Expected(ECode err_code, const std::string &err_msg) : Error(err_code, err_msg) {}

  T val{};
};
}  // namespace bambox