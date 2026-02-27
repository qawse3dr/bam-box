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

#include "util/WebDAV.hpp"

#include <spdlog/spdlog.h>

using bambox::Error;
using bambox::WebDAV;

WebDAV::WebDAV(const std::string& url, const std::string& user, const std::string& password)
    : url_(url), user_(user), password_(password) {}

std::string WebDAV::encode_url(const std::string& path) {
  std::string url = "";
  for (char c : url_ + "/" + path) {
    // for now only deal with spaces
    if (isspace(c)) {
      url += "%20";
    } else {
      url += c;
    }
  }
  return url;
}
Error WebDAV::create_dir(const std::string& path) {
  std::string url = encode_url(path);

  CURL* curl = curl_easy_init();
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
  curl_easy_setopt(curl, CURLOPT_USERNAME, user_.c_str());
  curl_easy_setopt(curl, CURLOPT_PASSWORD, password_.c_str());
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "MKCOL");
  CURLcode res = curl_easy_perform(curl);
  curl_easy_cleanup(curl);

  spdlog::info("Created directory {} with return: {}", path, curl_easy_strerror(res));
  // TODO error out
  return {};
}

Error WebDAV::upload_file(const std::string& remote_path, const std::string& path) {
  FILE* file = fopen(path.c_str(), "rb");
  fseek(file, 0L, SEEK_END);
  long filesize = ftell(file);
  rewind(file);

  std::string url = encode_url(remote_path);
  CURL* curl = curl_easy_init();
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
  curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
  curl_easy_setopt(curl, CURLOPT_USERNAME, user_.c_str());
  curl_easy_setopt(curl, CURLOPT_PASSWORD, password_.c_str());
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_READDATA, file);
  curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)filesize);
  CURLcode res = curl_easy_perform(curl);
  curl_easy_cleanup(curl);
  fclose(file);

  // TODO error out
  spdlog::info("Uploaded {} with return: {}", path, curl_easy_strerror(res));
  return {};
}
