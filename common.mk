ifndef QCONFIG
QCONFIG=qconfig.mk
endif
include $(QCONFIG)

NAME=bam-box

CFLAGS=-std=c++20 -Wall -Werror
LIBS+=asound

include $(MKFILES_ROOT)/qtargets.mk
