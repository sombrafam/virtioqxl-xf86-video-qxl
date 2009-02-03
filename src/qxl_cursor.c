/*
 * Copyright 2009 Red Hat, Inc.
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

#include "qxl.h"

static inline uint64_t
physical_address (qxlScreen *qxl, void *virtual)
{
    return (uint64_t) (virtual + (qxl->ram_physical - qxl->ram));
}

static void
push_cursor (qxlScreen *qxl, struct qxl_cursor_cmd *cursor)
{
    struct qxl_command cmd;

    cmd.type = QXL_CMD_CURSOR;
    cmd.data = physical_address (qxl, cursor);

    qxl_ring_push (qxl->cursor_ring, &cmd);
}

static struct qxl_cursor_cmd *
qxl_alloc_cursor_cmd(qxlScreen *qxl)
{
    return qxl_alloc(qxl->mem, sizeof(struct qxl_cursor_cmd));
}

static void
qxlSetCursorPosition(ScrnInfoPtr pScrn, int x, int y)
{
    qxlScreen *qxl = pScrn->driverPrivate;
    struct qxl_cursor_cmd *cursor = qxl_alloc_cursor_cmd(qxl);

    cursor->type = QXL_CURSOR_MOVE;
    cursor->u.position.x = qxl->cur_x = x;
    cursor->u.position.y = qxl->cur_y = y;

    push_cursor(qxl, cursor);
}

static void
qxlLoadCursorImage(ScrnInfoPtr pScrn, unsigned char *bits)
{
}

static void
qxlSetCursorColors(ScrnInfoPtr pScrn, int bg, int fg)
{
}

static void
qxlHideCursor(ScrnInfoPtr pScrn)
{
    qxlScreen *qxl = pScrn->driverPrivate;
    struct qxl_cursor_cmd *cursor = qxl_alloc_cursor_cmd(qxl);

    cursor->type = QXL_CURSOR_HIDE;

    push_cursor(qxl, cursor);
}

static void
qxlShowCursor(ScrnInfoPtr pScrn)
{
    /*
     * slightly hacky, but there's no QXL_CURSOR_SHOW.  Could maybe do
     * QXL_CURSOR_SET?
     */
    qxlScreen *qxl = pScrn->driverPrivate;
    qxlSetCursorPosition(pScrn, qxl->cur_x, qxl->cur_y);
}

hidden void
qxlCursorInit(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    xf86CursorInfoPtr cursor;

    cursor = xcalloc(1, sizeof(xf86CursorInfoRec));
    if (!cursor)
	return;

    cursor->MaxWidth = cursor->MaxHeight = 64;
    /* cursor->Flags; */
    cursor->SetCursorPosition = qxlSetCursorPosition;
    /* cursor->LoadCursorARGB = qxlLoadCursorARGB; */
    /* cursor->UseHWCursorARGB = qxlUseHWCursorARGB; */
    cursor->LoadCursorImage = qxlLoadCursorImage;
    cursor->SetCursorColors = qxlSetCursorColors;
    cursor->HideCursor = qxlHideCursor;
    cursor->ShowCursor = qxlShowCursor;

    if (!xf86InitCursor(pScreen, cursor))
	xfree(cursor);
}
