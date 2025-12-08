ifndef QCONFIG
QCONFIG=qconfig.mk
endif
include $(QCONFIG)

NAME=bam-box

CFLAGS=-std=c++20 -Wall -Werror
LIBS+=asound spdlog socket

EXTRA_SRCVPATH+=$(PROJECT_ROOT)/platform/rpi4/
EXTRA_LIBVPATH+=$(QNX_TARGET)/$(CPUDIR)/usr/local/lib
EXTRA_INCVPATH+=$(QNX_TARGET)/usr/local/include


include $(MKFILES_ROOT)/qtargets.mk
