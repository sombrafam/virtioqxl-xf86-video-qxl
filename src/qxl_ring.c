/*
 * Copyright 2009, 2010 Red Hat, Inc.
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
 *
 */
/** \file qxl_ring.c
 * \author Søren Sandmann <sandmann@redhat.com>
 */
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sched.h>
#include "qxl.h"

struct ring
{
    struct qxl_ring_header	header;
    uint8_t			elements[0];
};

struct qxl_ring
{
    const char *label;
    int type;
    volatile struct ring *ring;
    int			element_size;
    int			n_elements;
    int			io_port_prod_notify;
    qxl_screen_t    *qxl;
};

#ifdef VIRTIO_QXL
static void
update_cursor_ring(qxl_screen_t *qxl) {
    char *push1 = (char *)qxl->cursor_ring->ring;
    char *pull = push1 + sizeof(uint32_t) * 3;
    char *push2 = pull + sizeof(uint32_t);

    virtioqxl_push_ram(qxl, (void *)push2,
                       sizeof(QXLCursorRing) - sizeof(uint32_t) * 4);
    virtioqxl_push_ram(qxl, (void *)push1, sizeof(uint32_t) * 3);
    virtioqxl_pull_ram(qxl, (void *)pull, sizeof(uint32_t));
}

static void
update_command_ring(qxl_screen_t *qxl) {
    char *push1 = (char *)qxl->command_ring->ring;
    char *pull = push1 + sizeof(uint32_t) * 3;
    char *push2 = pull + sizeof(uint32_t);

    virtioqxl_push_ram(qxl, (void *)push2,
                       sizeof(QXLCommandRing) - sizeof(uint32_t) * 4);
    virtioqxl_push_ram(qxl, (void *)push1, sizeof(uint32_t) * 3);
    virtioqxl_pull_ram(qxl, (void *)pull, sizeof(uint32_t));
}

static void
update_release_ring(qxl_screen_t *qxl) {
    char *pull1 = (char *)qxl->release_ring->ring;
    char *push = pull1 + sizeof(uint32_t) * 3;
    char *pull2 = push + sizeof(uint32_t);

    virtioqxl_pull_ram(qxl, (void *)pull2,
                       sizeof(QXLReleaseRing) - sizeof(uint32_t) * 4);
    virtioqxl_pull_ram(qxl, (void *)pull1, sizeof(uint32_t) * 3);
    virtioqxl_push_ram(qxl, (void *)push, sizeof(uint32_t));
}
#endif

struct qxl_ring *
qxl_ring_create (struct qxl_ring_header *header,
		 int                     element_size,
		 int                     n_elements,
		 int			 io_port_prod_notify,
		 qxl_screen_t           *qxl,
        const char *label)
{
    struct qxl_ring *ring;

    ring = malloc (sizeof *ring);
    if (!ring)
	return NULL;

    if(strcmp(label,"command") == 0)
        ring->type = COMMAND_RING;
    if(strcmp(label,"cursor") == 0)
        ring->type = CURSOR_RING;
    if(strcmp(label,"release") == 0)
        ring->type = RELEASE_RING;

    ring->label = label;
    ring->ring = (volatile struct ring *)header;
    ring->element_size = element_size;
    ring->n_elements = n_elements;
    ring->io_port_prod_notify = io_port_prod_notify;
    ring->qxl = qxl;
    return ring;
}

#ifdef VIRTIO_QXL
static void
qxl_ring_push_command(struct qxl_ring *ring, struct QXLCommand *cmd)
{
    qxl_screen_t *qxl = ring->qxl;

    switch (cmd->type) {
        case QXL_CMD_SURFACE:
        {
            int stride;
            QXLSurface surf;
            uint8_t *ptr;
            QXLSurfaceCmd *c = virtual_address(qxl, (void *)cmd->data,
                                               qxl->main_mem_slot);

            virtioqxl_push_ram(qxl, (void *)c, sizeof(*c));

            if (c->type == QXL_SURFACE_CMD_DESTROY) {
                break;
            }

            surf = c->u.surface_create;
            stride = abs(surf.stride);
            ptr = virtual_address(qxl, (void *)surf.data, qxl->vram_mem_slot);
            virtioqxl_push_ram(qxl, (void *)ptr,
                               surf.height * stride + stride);
            break;
        }
        case QXL_CMD_DRAW:
        {
            QXLPHYSICAL addr;
            QXLImage *image;
            QXLDataChunk *chunk;
            QXLDrawable *draw = virtual_address(qxl, (void *)cmd->data,
                                                qxl->main_mem_slot);

            virtioqxl_push_ram(qxl, (void *)draw, sizeof(*draw));

            if (draw->type != QXL_DRAW_COPY) {
                break;
            }

            image = virtual_address(qxl, (void *)draw->u.copy.src_bitmap,
                                    qxl->main_mem_slot);
            virtioqxl_push_ram(qxl, (void *)image, sizeof(*image));

            if (image->descriptor.type == SPICE_IMAGE_TYPE_SURFACE) {
                break;
            }

            if (image->bitmap.flags & QXL_BITMAP_DIRECT) {
                uint8_t *ptr = virtual_address(qxl, (void *)image->bitmap.data,
                                               qxl->main_mem_slot);
                virtioqxl_push_ram(qxl, (void *)ptr,
                        image->descriptor.height * image->bitmap.stride);
                break;
            }
            addr = image->bitmap.data;
            while (addr) {
                chunk = virtual_address(qxl, (void *)addr, qxl->main_mem_slot);
                virtioqxl_push_ram(qxl, (void *)chunk,
                                   sizeof(*chunk) + chunk->data_size);
                addr = chunk->next_chunk;
            }
            break;
        }
        case QXL_CMD_CURSOR:
        {
            QXLCursor *cursor;
            QXLCursorCmd *c = virtual_address(qxl, (void *)cmd->data,
                                              qxl->main_mem_slot);

            virtioqxl_push_ram(qxl, (void *)c, sizeof(*c));

            if (c->type != QXL_CURSOR_SET) {
                break;
            }

            cursor = virtual_address(qxl, (void *)c->u.set.shape,
                                     qxl->main_mem_slot);
            virtioqxl_push_ram(qxl, (void *)cursor,
                               sizeof(*cursor) + cursor->data_size);
            break;
        }
    }
}
#endif // VIRTIO_QXL

void
qxl_ring_push (struct qxl_ring *ring,
	       const void      *new_elt)
{
    volatile struct qxl_ring_header *header = &(ring->ring->header);
    volatile uint8_t *elt;
    int idx;
    struct QXLCommand *cmd = (QXLCommand *)new_elt;
    struct QXLRam *ram = get_ram_header(ring->qxl);

#ifdef DEBUG_LOG_COMMAND
    qxl_log_command(ring->qxl, cmd, "");
#endif

#ifdef VIRTIO_QXL
    if(ring->type == CURSOR_RING){
        // When the guest stop sending cursor commands, the host side
        // consumer(libspice thread) sleeps. This avoid a delay when starting
        // to move the mouse again.
        if(SPICE_RING_IS_EMPTY(&ram->cursor_ring)){
            ioport_write(ring->qxl, QXL_IO_NOTIFY_CURSOR, 0);
        }
    }
    qxl_ring_push_command(ring, cmd);
#endif

    while (header->prod - header->cons == header->num_items)
    {
#ifdef VIRTIO_QXL
        ioport_write(ring->qxl, QXL_IO_NOTIFY_CURSOR, 0);
        update_command_ring(ring->qxl);
        update_cursor_ring(ring->qxl);
        sched_yield();
#endif
        header->notify_on_cons = header->cons + 1;
#ifdef XSPICE
	/* in gtkperf, circles, this is a major bottleneck. Can't be that good in a vm either
	 * Adding the yield reduces cpu usage, but doesn't improve throughput. */
        sched_yield();
#endif
	mem_barrier();
    }

    idx = header->prod & (ring->n_elements - 1);
    elt = ring->ring->elements + idx * ring->element_size;

    memcpy((void *)elt, new_elt, ring->element_size);

    header->prod++;

    mem_barrier();

#ifdef VIRTIO_QXL
    update_command_ring(ring->qxl);
    update_cursor_ring(ring->qxl);
#endif

    if (header->prod == header->notify_on_prod) {
        ioport_write (ring->qxl, ring->io_port_prod_notify, 0);
    }
}

Bool
qxl_ring_pop (struct qxl_ring *ring,
	      void            *element)
{
    volatile struct qxl_ring_header *header = &(ring->ring->header);
    volatile uint8_t *ring_elt;
    int idx;

#ifdef VIRTIO_QXL
    update_release_ring(ring->qxl);
#endif

    if (header->cons == header->prod)
	return FALSE;

    idx = header->cons & (ring->n_elements - 1);
    ring_elt = ring->ring->elements + idx * ring->element_size;

    memcpy (element, (void *)ring_elt, ring->element_size);

    header->cons++;

    return TRUE;
}

void
qxl_ring_wait_idle (struct qxl_ring *ring)
{
    while (ring->ring->header.cons != ring->ring->header.prod)
    {
	usleep (1000);
	mem_barrier();
    }
}
