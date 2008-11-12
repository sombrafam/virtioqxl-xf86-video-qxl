/*
 * Copyright 2008 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "config.h"

#include "xf86.h"
#include "xf86Resources.h"
#include "xf86PciInfo.h"
#include "xf86Cursor.h"
#include "xf86_OSproc.h"
#include "exa.h"
#include "xf86xv.h"
#ifdef XSERVER_PCIACCESS
#include "pciaccess.h"
#endif

#define hidden _X_HIDDEN

#define PCI_VENDOR_QUMRANET	0x1af4

#define PCI_CHIP_QXL_0100	0x0100

/* I/O port definitions */
enum {
    QXL_IO_NOTIFY_CMD,
    QXL_IO_NOTIFY_CURSOR,
    QXL_IO_UPDATE_AREA,
    QXL_IO_UPDATE_IRQ,
    QXL_IO_NOTIFY_OOM,
    QXL_IO_RESET,
    QXL_IO_SET_MODE,
    QXL_IO_LOG,
};

struct qxl_mode {
    unsigned int id;
    unsigned int x_res;
    unsigned int y_res;
    unsigned int bits;
    unsigned int stride;
    unsigned int x_mili;
    unsigned int y_mili;
    unsigned int orientation;
};

typedef struct _qxlScreen
{
    /* qxl calls these ram, vram, and rom */
    void *			cram; /* Command RAM */
    void *			vram; /* Video RAM */
    void *			pram; /* Parameter RAM */

    /* mode list */
    int				num_modes;
    struct qxl_mode *		modes;

    int				io_base;

    EntityInfoPtr		entity;

#ifdef XSERVER_LIBPCIACCESS
    struct pci_device *		pci;
#else
    pciVideoPtr			pci;
    PCITAG			pciTag;
#endif

    CloseScreenProcPtr		CloseScreen;
} qxlScreen;
