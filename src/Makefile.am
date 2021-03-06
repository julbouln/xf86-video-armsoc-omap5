#  Copyright © 2011 Texas Instruments Incorporated
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
#  ADAM JACKSON BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
#  IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
#  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
# Authors:
#    Ian Elliott <ianelliottus@yahoo.com>
#    Rob Clark <rob@ti.com>

# this is obnoxious:
# -module lets us name the module exactly how we want
# -avoid-version prevents gratuitous .0.0.0 version numbers on the end
# _ladir passes a dummy rpath to libtool so the thing will actually link
# TODO: -nostdlib/-Bstatic/-lgcc platform magic, not installing the .a, etc.


NEON_CFLAGS=-march=armv7-a -mfpu=neon -mfloat-abi=hard -Wa,-mfpu=neon

NEON_CCASFLAGS=$(NEON_CFLAGS) -mthumb-interwork
NEON_ASFLAGS=-k -mcpu=cortex-a15 $(NEON_CCASFLAGS)

AM_ASFLAGS = $(NEON_ASFLAGS)
AM_CCASFLAGS = $(NEON_CCASFLAGS)

# TODO: probably should have configure script check for what is supported..
ERROR_CFLAGS = -Wall -Wdeclaration-after-statement -Wvla \
	-Wpointer-arith -Wmissing-declarations -Wmissing-prototypes \
	-Wwrite-strings -Wformat-nonliteral  -Wformat-security \
	-Wold-style-definition -Winit-self -Wmissing-include-dirs \
	-Waddress -Waggregate-return -Wno-multichar -Wnested-externs
 
AM_CFLAGS = @XORG_CFLAGS@
omap5_drv_la_LTLIBRARIES = omap5_drv.la
omap5_drv_la_LDFLAGS = -module -avoid-version -no-undefined
omap5_drv_la_LIBADD = @XORG_LIBS@
omap5_drv_ladir = @moduledir@/drivers
DRMMODE_SRCS = drmmode_omap/drmmode_omap.c

omap5_drv_la_SOURCES = \
         drmmode_display.c \
         armsoc_exa.c \
         armsoc_exa_null.c \
         armsoc_dri2.c \
         armsoc_driver.c \
         armsoc_dumb.c \
         armsoc_dri3.c \
         armsoc_present.c \
         armsoc_xv.c \
         viv2d/queue.c \
         viv2d/etnaviv_extra.c \
         viv2d/viv2d_exa.c \
         $(DRMMODE_SRCS)
