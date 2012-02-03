/*
 * qxl command logging -- for debug purposes
 *
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * maintained by Gerd Hoffmann <kraxel@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qxl.h"

#define to_llu(x)((long long unsigned)(x))

static const char *qxl_type[] = {
    [ QXL_CMD_NOP ]     = "NOP",
    [ QXL_CMD_DRAW ]    = "DRAW",
    [ QXL_CMD_UPDATE ]  = "UPDATE",
    [ QXL_CMD_CURSOR ]  = "CURSOR",
    [ QXL_CMD_MESSAGE ] = "MESSAGE",
    [ QXL_CMD_SURFACE ] = "SURFACE",
};

static const char *qxl_draw_type[] = {
    [ QXL_DRAW_NOP         ] = "nop",
    [ QXL_DRAW_FILL        ] = "fill",
    [ QXL_DRAW_OPAQUE      ] = "opaque",
    [ QXL_DRAW_COPY        ] = "copy",
    [ QXL_COPY_BITS        ] = "copy-bits",
    [ QXL_DRAW_BLEND       ] = "blend",
    [ QXL_DRAW_BLACKNESS   ] = "blackness",
    [ QXL_DRAW_WHITENESS   ] = "whitemess",
    [ QXL_DRAW_INVERS      ] = "invers",
    [ QXL_DRAW_ROP3        ] = "rop3",
    [ QXL_DRAW_STROKE      ] = "stroke",
    [ QXL_DRAW_TEXT        ] = "text",
    [ QXL_DRAW_TRANSPARENT ] = "transparent",
    [ QXL_DRAW_ALPHA_BLEND ] = "alpha-blend",
};

static const char *qxl_draw_effect[] = {
    [ QXL_EFFECT_BLEND            ] = "blend",
    [ QXL_EFFECT_OPAQUE           ] = "opaque",
    [ QXL_EFFECT_REVERT_ON_DUP    ] = "revert-on-dup",
    [ QXL_EFFECT_BLACKNESS_ON_DUP ] = "blackness-on-dup",
    [ QXL_EFFECT_WHITENESS_ON_DUP ] = "whiteness-on-dup",
    [ QXL_EFFECT_NOP_ON_DUP       ] = "nop-on-dup",
    [ QXL_EFFECT_NOP              ] = "nop",
    [ QXL_EFFECT_OPAQUE_BRUSH     ] = "opaque-brush",
};

static const char *qxl_surface_cmd[] = {
   [ QXL_SURFACE_CMD_CREATE  ] = "create",
   [ QXL_SURFACE_CMD_DESTROY ] = "destroy",
};

static const char *spice_surface_fmt[] = {
   [ SPICE_SURFACE_FMT_INVALID  ] = "invalid",
   [ SPICE_SURFACE_FMT_1_A      ] = "alpha/1",
   [ SPICE_SURFACE_FMT_8_A      ] = "alpha/8",
   [ SPICE_SURFACE_FMT_16_555   ] = "555/16",
   [ SPICE_SURFACE_FMT_16_565   ] = "565/16",
   [ SPICE_SURFACE_FMT_32_xRGB  ] = "xRGB/32",
   [ SPICE_SURFACE_FMT_32_ARGB  ] = "ARGB/32",
};

static const char *qxl_cursor_cmd[] = {
   [ QXL_CURSOR_SET   ] = "set",
   [ QXL_CURSOR_MOVE  ] = "move",
   [ QXL_CURSOR_HIDE  ] = "hide",
   [ QXL_CURSOR_TRAIL ] = "trail",
};

static const char *spice_cursor_type[] = {
   [ SPICE_CURSOR_TYPE_ALPHA   ] = "alpha",
   [ SPICE_CURSOR_TYPE_MONO    ] = "mono",
   [ SPICE_CURSOR_TYPE_COLOR4  ] = "color4",
   [ SPICE_CURSOR_TYPE_COLOR8  ] = "color8",
   [ SPICE_CURSOR_TYPE_COLOR16 ] = "color16",
   [ SPICE_CURSOR_TYPE_COLOR24 ] = "color24",
   [ SPICE_CURSOR_TYPE_COLOR32 ] = "color32",
};

static const char *
qxl_v2n(const char *n[], size_t l, int v)
{
    if (v >= l || !n[v]) {
        return "???";
    }
    return n[v];
}
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

#define qxl_name(_list, _value) qxl_v2n(_list, ARRAY_SIZE(_list), _value)

static void
qxl_log_image(qxl_screen_t *qxl, QXLPHYSICAL addr, int group_id)
{
    QXLImage *image;
    QXLImageDescriptor *desc;

    image = virtual_address(qxl,(void *)addr,group_id);
    desc = &image->descriptor;

    fprintf(stderr," (id %" PRIx64 " type %d flags %d width %d height %d",
            desc->id, desc->type, desc->flags, desc->width, desc->height);

    switch (desc->type) {
    case SPICE_IMAGE_TYPE_BITMAP:
        fprintf(stderr,", fmt %d flags %d x %d y %d stride %d"
                " palette %" PRIx64,
                image->bitmap.format, image->bitmap.flags,
                image->bitmap.x, image->bitmap.y,
                image->bitmap.stride,
                image->bitmap.palette);
        break;
    }
    fprintf(stderr,")");
}

static void
qxl_log_rect(QXLRect *rect)
{
    fprintf(stderr," %dx%d+%d+%d",
            rect->right - rect->left,
            rect->bottom - rect->top,
            rect->left, rect->top);
}

static void
qxl_log_cmd_draw_copy(qxl_screen_t *qxl, QXLCopy *copy, int group_id)
{
    int offset;
    char *data;

    data = virtual_address(qxl,(void *)copy->src_bitmap,group_id);
    offset = physical_address(qxl,data, 0);

    fprintf(stderr," src @ %d",offset);

    qxl_log_image(qxl, copy->src_bitmap, group_id);
    fprintf(stderr," area");
    qxl_log_rect(&copy->src_area);
    fprintf(stderr," rop %d", copy->rop_descriptor);
}

static void
qxl_log_cmd_draw(qxl_screen_t *qxl, QXLDrawable *draw, int group_id)
{
    fprintf(stderr,"surface_id %d type %s effect %s",
            draw->surface_id,
            qxl_name(qxl_draw_type, draw->type),
            qxl_name(qxl_draw_effect, draw->effect));

    if (draw->type != QXL_DRAW_COPY) {
        return;
    }

    qxl_log_cmd_draw_copy(qxl, &draw->u.copy, group_id);
}

static void
qxl_log_cmd_surface(qxl_screen_t *qxl, QXLSurfaceCmd *cmd)
{
    uint64_t offset;

    fprintf(stderr," %s id %d",
            qxl_name(qxl_surface_cmd, cmd->type),
            cmd->surface_id);

    if (cmd->type == QXL_SURFACE_CMD_DESTROY) {
        return;
    }

    offset = cmd->u.surface_create.data;
    fprintf(stderr," size %dx%d stride %d format %s data @ %llu",
            cmd->u.surface_create.width,
            cmd->u.surface_create.height,
            cmd->u.surface_create.stride,
            qxl_name(spice_surface_fmt, cmd->u.surface_create.format),
            to_llu(offset));
}

static void
qxl_log_cmd_cursor(qxl_screen_t *qxl, QXLCursorCmd *cmd, int group_id)
{
    QXLCursor *cursor;
    int offset;

    fprintf(stderr," %s", qxl_name(qxl_cursor_cmd, cmd->type));

    switch (cmd->type) {
    case QXL_CURSOR_SET:
        cursor = virtual_address(qxl, (void *)cmd->u.set.shape, group_id);
        //FIXME: Should print the offset for PCI address as well
        offset = physical_address(qxl,cursor,qxl->main_mem_slot);

        fprintf(stderr," +%d+%d visible %s, shape @ %d",
                cmd->u.set.position.x,
                cmd->u.set.position.y,
                cmd->u.set.visible ? "yes" : "no",
                offset);

        fprintf(stderr," type %s size %dx%d hot-spot +%d+%d"
                " unique 0x%" PRIx64 " data-size %d",
                qxl_name(spice_cursor_type, cursor->header.type),
                cursor->header.width, cursor->header.height,
                cursor->header.hot_spot_x, cursor->header.hot_spot_y,
                cursor->header.unique, cursor->data_size);
        break;
    case QXL_CURSOR_MOVE:
        fprintf(stderr," +%d+%d", cmd->u.position.x, cmd->u.position.y);
        break;
    }
}

void
qxl_log_command(qxl_screen_t *qxl, QXLCommand *cmd, char *direction)
{
    void *data;

    data = virtual_address (qxl,(void *)cmd->data,qxl->main_mem_slot);
    //FIXME: Should print the offset for PCI address as well
    fprintf(stderr,"%sGUEST DEBUGQXL: cmd: %s @ %llu: ",
            direction,qxl_name(qxl_type, cmd->type),to_llu(cmd->data));

    switch (cmd->type) {
    case QXL_CMD_SURFACE:
        qxl_log_cmd_surface(qxl, data);
        break;
    case QXL_CMD_CURSOR:
        qxl_log_cmd_cursor(qxl, data, qxl->main_mem_slot);
        break;
    case QXL_CMD_DRAW:
        qxl_log_cmd_draw(qxl, data, qxl->main_mem_slot);
        break;
    };
    fprintf(stderr,"\n");
}
