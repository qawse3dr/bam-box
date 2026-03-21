ifndef QCONFIG
QCONFIG=qconfig.mk
endif
include $(QCONFIG)

NAME=bam-box

INSTALLDIR=usr/local/lib

CFLAGS=-std=c++20 -Wall -Werror
LIBS+=asound spdlog socket screen discid curl

EXTRA_SRCVPATH+=$(PROJECT_ROOT)/platform/rpi4/
EXTRA_SRCVPATH+=$(PROJECT_ROOT)/util
LATE_SRCVPATH+=$(PROJECT_ROOT)/gen

EXTRA_LIBVPATH+=$(QNX_TARGET)/$(CPUDIR)/usr/local/lib
EXTRA_INCVPATH+=$(QNX_TARGET)/usr/local/include

# GTK includes
GTK4_INSTALL_ROOT=/workspaces/qnx-dev/staging/$(CPUDIR)/usr/local
EXTRA_INCVPATH += $(GTK4_INSTALL_ROOT)/include/
EXTRA_INCVPATH += $(GTK4_INSTALL_ROOT)/include/gtk-4.0
EXTRA_INCVPATH += $(GTK4_INSTALL_ROOT)/include/glib-2.0
EXTRA_INCVPATH += $(GTK4_INSTALL_ROOT)/lib/glib-2.0/include
EXTRA_INCVPATH += $(GTK4_INSTALL_ROOT)/include/pango-1.0
EXTRA_INCVPATH += $(GTK4_INSTALL_ROOT)/include/harfbuzz
EXTRA_INCVPATH += $(GTK4_INSTALL_ROOT)/include/cairo
EXTRA_INCVPATH += $(GTK4_INSTALL_ROOT)/include/gdk-pixbuf-2.0
EXTRA_INCVPATH += $(GTK4_INSTALL_ROOT)/include/atk-1.0
EXTRA_INCVPATH += $(GTK4_INSTALL_ROOT)/include/graphene-1.0
EXTRA_INCVPATH += $(GTK4_INSTALL_ROOT)/lib/graphene-1.0/include
EXTRA_LIBVPATH += $(GTK4_INSTALL_ROOT)/lib
LIBS += gtk-4 glib-2.0 gobject-2.0 gio-2.0 cairo screen m epoxy intl gdk_pixbuf-2.0 pango-1.0 FLAC++ FLAC

EXTRA_OBJS = icons.o

include $(MKFILES_ROOT)/qtargets.mk

$(PROJECT_ROOT)/gen/icons.c: $(PROJECT_ROOT)/ui/bambox.gresource.xml $(PROJECT_ROOT)/ui/bambox.css $(PROJECT_ROOT)/ui/bambox_light.css $(PROJECT_ROOT)/ui/bambox_dark.css $(PROJECT_ROOT)/ui/bambox.ui
	glib-compile-resources $< --sourcedir=$(PROJECT_ROOT)/ui --generate --target $@
