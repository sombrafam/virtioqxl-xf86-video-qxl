#  Copyright 2008 Red Hat, Inc.
#
#  Permission is hereby granted, free of charge, to any person obtaining a
#  copy of this software and associated documentation files (the "Software"),
#  to deal in the Software without restriction, including without limitation
#  on the rights to use, copy, modify, merge, publish, distribute, sub
#  license, and/or sell copies of the Software, and to permit persons to whom
#  the Software is furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice (including the next
#  paragraph) shall be included in all copies or substantial portions of the
#  Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
#  THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
#  IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
#  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


# this is obnoxious:
# -module lets us name the module exactly how we want
# -avoid-version prevents gratuitous .0.0.0 version numbers on the end
# _ladir passes a dummy rpath to libtool so the thing will actually link
# TODO: -nostdlib/-Bstatic/-lgcc platform magic, not installing the .a, etc.

SUBDIRS=uxa

AM_CFLAGS = $(SPICE_PROTOCOL_CFLAGS) $(XORG_CFLAGS) $(PCIACCESS_CFLAGS) $(CWARNFLAGS)

if BUILD_QXL
qxl_drv_la_LTLIBRARIES = qxl_drv.la
qxl_drv_la_LDFLAGS = -module -avoid-version
qxl_drv_ladir = @moduledir@/drivers

qxl_drv_la_LIBADD = uxa/libuxa.la

qxl_drv_la_SOURCES =				\
	qxl.h					\
	qxl_driver.c				\
	qxl_image.c				\
	qxl_surface.c				\
	qxl_ring.c				\
	qxl_mem.c				\
	mspace.c				\
	mspace.h				\
	murmurhash3.c				\
	murmurhash3.h				\
	qxl_cursor.c				\
	qxl_logger.c
endif

if BUILD_VIRTIO_QXL
virtioqxl_drv_la_LTLIBRARIES = virtioqxl_drv.la
virtioqxl_drv_la_LDFLAGS = -module -avoid-version
virtioqxl_drv_ladir = @moduledir@/drivers

virtioqxl_drv_la_CFLAGS = -DVIRTIO_QXL $(AM_CFLAGS)

virtioqxl_drv_la_LIBADD = uxa/libuxa.la

virtioqxl_drv_la_SOURCES =				\
	qxl.h					\
	qxl_driver.c				\
	qxl_image.c				\
	qxl_surface.c				\
	qxl_ring.c				\
	qxl_mem.c				\
	mspace.c				\
	mspace.h				\
	murmurhash3.c				\
	murmurhash3.h				\
	qxl_logger.c				\
	qxl_cursor.c
endif

qxl_drv_la_CFLAGS = $(AM_CFLAGS) -g -O0

if BUILD_XSPICE
spiceqxl_drv_la_LTLIBRARIES = spiceqxl_drv.la
spiceqxl_drv_la_LDFLAGS = -module -avoid-version $(SPICE_LIBS)
spiceqxl_drv_ladir = @moduledir@/drivers

spiceqxl_drv_la_CFLAGS = -DXSPICE $(AM_CFLAGS) $(SPICE_CFLAGS)

spiceqxl_drv_la_LIBADD = uxa/libuxa.la

spiceqxl_drv_la_SOURCES =				\
	qxl.h					\
	qxl_option_helpers.c		\
	spiceqxl_spice_server.c			\
	spiceqxl_spice_server.h			\
	spiceqxl_io_port.c			\
	spiceqxl_io_port.h			\
	spiceqxl_driver.c			\
	spiceqxl_driver.h			\
	spiceqxl_main_loop.c			\
	spiceqxl_main_loop.h		\
	spiceqxl_display.c			\
	spiceqxl_display.h			\
	spiceqxl_inputs.c			\
	spiceqxl_inputs.h			\
	qxl_driver.c				\
	qxl_image.c				\
	qxl_surface.c				\
	qxl_ring.c				\
	qxl_mem.c				\
	mspace.c				\
	mspace.h				\
	murmurhash3.c				\
	murmurhash3.h				\
	qxl_cursor.c
endif
