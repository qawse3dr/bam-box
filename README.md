# Bam Box
Bam box is a cd player created from a RPI4 running QNX 8.0

## Hardware

## Building

### Prereqs
In order to build you first need to build and install spdlog which can be done doing the following with your SDP sourced.
```bash
git clone https://github.com/gabime/spdlog.git && cd spdlog
mkdir build_qnx
cmake \
  -DCMAKE_TOOLCHAIN_FILE=../../aarch64-qnx.cmake \
  -DCMAKE_INSTALL_PREFIX=${QNX_TARGET} \
  -DCMAKE_INSTALL_INCLUDEDIR=usr/local/include \
  -DCMAKE_INSTALL_LIBDIR=aarch64le/usr/local/lib \
  -DCMAKE_INSTALL_BINDIR=aarch64le/usr/local/bin \
  -DCMAKE_SHARED_LINKER_FLAGS=-lsocket \
  -DCMAKE_EXE_LINKER_FLAGS=-lsocket \
  ..
make install
```

TODO
- curl
- libdiscid
- gtk

### Building
This repo makes use of QNX's recursive make which can be build by simply typing make. After it is build the artifacts
will be created under nto/aarch64/o.le
```
JLEVEL=4 make
```

### Features

- [x] Playing music from CD
- [x] Display CD information
  - [x] Album Art
  - [x] Artist, Song, Album
  - [ ] Lyrics
- [x] Auto fetch album information and art from DB
- [x] Software Volume Controls
- [x] Audio output selection 
  - [x] PCM (headphones)
  - [x] I2S (speakers)
  - [ ] USB (usb headphone)
- [x] CD Eject
- [ ] Album track selection
- [ ] Settings
  - [ ] In UI update
    - [ ] Default output
    - [ ] Default Soft Volume
  - [ ] JSON file
- [ ] Dump FLAC files
- [ ] Upload to webdav server

### Bugs


### Improvements
- [ ] The design is super tightly coupled, might be better to make the cdreader and audio player singletons or create a state which allows control
- [ ] Split the screens into separate classes instead of being part of BamBox
- [ ] Finish custom BSP that can be build from a single cmd
- [ ] Make .ui and css resources to be loaded from