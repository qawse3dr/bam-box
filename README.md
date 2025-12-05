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

### Building
This repo makes use of QNX's recursive make which can be build by simply typing make. After it is build the artifacts
will be created under nto/aarch64/o.le
```
JLEVEL=4 make
```
