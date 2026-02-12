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

#include <pthread.h>
#include <signal.h>
#include <spdlog/spdlog.h>
#include <sys/neutrino.h>
#include <sys/syspage.h>

#include "BamBox.hpp"

int main(int argc, char* argv[]) {
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGQUIT);
  sigaddset(&set, SIGUSR1);
  pthread_sigmask(SIG_BLOCK, &set, NULL);

  spdlog::info("Welcome to bambox");
  bambox::BamBox bambox;

  bambox::BamBoxConfig cfg = parse_cli(argc, argv).val; // todo check error
  auto res = bambox.config(std::move(cfg));
  if (res.is_error()) {
    spdlog::error("Failed to configure bambox with: {}", res.msg);
    return -1;
  }
  bambox.go();

  int sig;
  while (sigwait(&set, &sig) != 0) {
  }
  spdlog::info("Stopping bambox");

  bambox.stop();

  return 0;
}