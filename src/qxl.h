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

#include <stdint.h>

#include "compiler.h"
#include "xf86.h"
#if GET_ABI_MAJOR(ABI_VIDEODRV_VERSION) < 6
#include "xf86Resources.h"
#endif
#include "xf86PciInfo.h"
#include "xf86Cursor.h"
#include "xf86_OSproc.h"
#include "xf86xv.h"
#include "shadow.h"
#include "micmap.h"
#ifdef XSERVER_PCIACCESS
#include "pciaccess.h"
#endif
#include "fb.h"

#define hidden _X_HIDDEN

#define QXL_NAME		"qxl"
#define QXL_DRIVER_NAME		"qxl"
#define PCI_VENDOR_RED_HAT	0x1b36

#define PCI_CHIP_QXL_0100	0x0100

#pragma pack(push,1)

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
    uint32_t id;
    uint32_t x_res;
    uint32_t y_res;
    uint32_t bits;
    uint32_t stride;
    uint32_t x_mili;
    uint32_t y_mili;
    uint32_t orientation;
};

typedef enum
{
    QXL_CMD_NOP,
    QXL_CMD_DRAW,
    QXL_CMD_UPDATE,
    QXL_CMD_CURSOR,
    QXL_CMD_MESSAGE
} qxl_command_type;

struct qxl_command {
    uint64_t data;
    uint32_t type;
    uint32_t pad;
};

struct qxl_rect {
    uint32_t top;
    uint32_t left;
    uint32_t bottom;
    uint32_t right;
};

union qxl_release_info {
    uint64_t id;
    uint64_t next;
};

struct qxl_clip {
    uint32_t type;
    uint64_t address;
};

struct qxl_point {
    int x;
    int y;
};

struct qxl_pattern {
    uint64_t pat;
    struct qxl_point pos;
};

typedef enum
{
    QXL_BRUSH_TYPE_NONE,
    QXL_BRUSH_TYPE_SOLID,
    QXL_BRUSH_TYPE_PATTERN
} qxl_brush_type;

struct qxl_brush {
    uint32_t type;
    union {
	uint32_t color;
	struct qxl_pattern pattern;
    } u;
};

struct qxl_mask {
    unsigned char flags;
    struct qxl_point pos;
    uint64_t bitmap;
};

typedef enum {
    QXL_IMAGE_TYPE_BITMAP,
    QXL_IMAGE_TYPE_QUIC,
    QXL_IMAGE_TYPE_PNG,
    QXL_IMAGE_TYPE_LZ_PLT = 100,
    QXL_IMAGE_TYPE_LZ_RGB,
    QXL_IMAGE_TYPE_GLZ_RGB,
    QXL_IMAGE_TYPE_FROM_CACHE,
} qxl_image_type;

typedef enum {
    QXL_IMAGE_CACHE = (1 << 0)
} qxl_image_flags;

struct qxl_image_descriptor
{
    uint64_t id;
    uint8_t type;
    uint8_t flags;
    uint32_t width;
    uint32_t height;
};

struct qxl_data_chunk {
    uint32_t data_size;
    uint64_t prev_chunk;
    uint64_t next_chunk;
    uint8_t data[0];
};

typedef enum
{
    QXL_BITMAP_FMT_INVALID,
    QXL_BITMAP_FMT_1BIT_LE,
    QXL_BITMAP_FMT_1BIT_BE,
    QXL_BITMAP_FMT_4BIT_LE,
    QXL_BITMAP_FMT_4BIT_BE,
    QXL_BITMAP_FMT_8BIT,
    QXL_BITMAP_FMT_16BIT,
    QXL_BITMAP_FMT_24BIT,
    QXL_BITMAP_FMT_32BIT,
    QXL_BITMAP_FMT_RGBA,
} qxl_bitmap_format;

typedef enum {
    QXL_BITMAP_PAL_CACHE_ME = (1 << 0),
    QXL_BITMAP_PAL_FROM_CACHE = (1 << 1),
    QXL_BITMAP_TOP_DOWN = (1 << 2),
} qxl_bitmap_flags;

struct qxl_bitmap {
    uint8_t format;
    uint8_t flags;		
    uint32_t x;			/* actually width */
    uint32_t y;			/* actually height */
    uint32_t stride;		/* in bytes */
    uint64_t palette;		/* Can be NULL */
    uint64_t data;		/* A qxl_data_chunk that actually contains the data */
};

struct qxl_image {
    struct qxl_image_descriptor descriptor;
    union
    {
	struct qxl_bitmap bitmap;
    } u;
};

struct qxl_fill {
    struct qxl_brush brush;
    unsigned short rop_descriptor;
    struct qxl_mask mask;
};

struct qxl_opaque {
    uint64_t src_bitmap;
    struct qxl_rect src_area;
    struct qxl_brush brush;
    unsigned short rop_descriptor;
    unsigned char scale_mode;
    struct qxl_mask mask;
};

struct qxl_copy {
    uint64_t src_bitmap;
    struct qxl_rect src_area;
    unsigned short rop_descriptor;
    unsigned char scale_mode;
    struct qxl_mask mask;
};

struct qxl_transparent {
    uint64_t src_bitmap;
    struct qxl_rect src_area;
    uint32_t src_color;
    uint32_t true_color;
};

struct qxl_alpha_blend {
    unsigned char alpha;
    uint64_t src_bitmap;
    struct qxl_rect src_area;
};

struct qxl_copy_bits {
    struct qxl_point src_pos;
};

struct qxl_blend { /* same as copy */
    uint64_t src_bitmap;
    struct qxl_rect src_area;
    unsigned short rop_descriptor;
    unsigned char scale_mode;
    struct qxl_mask mask;
};

struct qxl_rop3 {
    uint64_t src_bitmap;
    struct qxl_rect src_area;
    struct qxl_brush brush;
    unsigned char rop3;
    unsigned char scale_mode;
    struct qxl_mask mask;
};

struct qxl_line_attr {
    unsigned char flags;
    unsigned char join_style;
    unsigned char end_style;
    unsigned char style_nseg;
    int width;
    int miter_limit;
    uint64_t style;
};

struct qxl_stroke {
    uint64_t path;
    struct qxl_line_attr attr;
    struct qxl_brush brush;
    unsigned short fore_mode;
    unsigned short back_mode;
};

struct qxl_text {
    uint64_t str;
    struct qxl_rect back_area;
    struct qxl_brush fore_brush;
    struct qxl_brush back_brush;
    unsigned short fore_mode;
    unsigned short back_mode;
};

struct qxl_blackness {
    struct qxl_mask mask;
};

struct qxl_inverse {
    struct qxl_mask mask;
};

struct qxl_whiteness {
    struct qxl_mask mask;
};

/* Effects */
typedef enum
{
    QXL_EFFECT_BLEND,
    QXL_EFFECT_OPAQUE,
    QXL_EFFECT_REVERT_ON_DUP,
    QXL_EFFECT_BLACKNESS_ON_DUP,
    QXL_EFFECT_WHITENESS_ON_DUP,
    QXL_EFFECT_NOP_ON_DUP,
    QXL_EFFECT_NOP,
    QXL_EFFECT_OPAQUE_BRUSH
} qxl_effect_type;

typedef enum
{
    QXL_CLIP_TYPE_NONE,
    QXL_CLIP_TYPE_RECTS,
    QXL_CLIP_TYPE_PATH,
} qxl_clip_type;

typedef enum {
    QXL_DRAW_NOP,
    QXL_DRAW_FILL,
    QXL_DRAW_OPAQUE,
    QXL_DRAW_COPY,
    QXL_COPY_BITS,
    QXL_DRAW_BLEND,
    QXL_DRAW_BLACKNESS,
    QXL_DRAW_WHITENESS,
    QXL_DRAW_INVERS,
    QXL_DRAW_ROP3,
    QXL_DRAW_STROKE,
    QXL_DRAW_TEXT,
    QXL_DRAW_TRANSPARENT,
    QXL_DRAW_ALPHA_BLEND,
} qxl_draw_type;

struct qxl_drawable {
    union qxl_release_info release_info;
    unsigned char effect;
    unsigned char type;
    unsigned short bitmap_offset;
    struct qxl_rect bitmap_area;
    struct qxl_rect bbox;
    struct qxl_clip clip;
    uint32_t mm_time;
    union {
	struct qxl_fill fill;
	struct qxl_opaque opaque;
	struct qxl_copy copy;
	struct qxl_transparent transparent;
	struct qxl_alpha_blend alpha_blend;
	struct qxl_copy_bits copy_bits;
	struct qxl_blend blend;
	struct qxl_rop3 rop3;
	struct qxl_stroke stroke;
	struct qxl_text text;
	struct qxl_blackness blackness;
	struct qxl_inverse inverse;
	struct qxl_whiteness whiteness;
    } u;
};

struct qxl_update_cmd {
    union qxl_release_info release_info;
    struct qxl_rect area;
    uint32_t update_id;
};

struct qxl_point16 {
    int16_t x;
    int16_t y;
};

enum {
    QXL_CURSOR_SET,
    QXL_CURSOR_MOVE,
    QXL_CURSOR_HIDE,
    QXL_CURSOR_TRAIL,
};

#define QXL_CURSOR_DEVICE_DATA_SIZE 128

enum {
    CURSOR_TYPE_ALPHA,
    CURSOR_TYPE_MONO,
    CURSOR_TYPE_COLOR4,
    CURSOR_TYPE_COLOR8,
    CURSOR_TYPE_COLOR16,
    CURSOR_TYPE_COLOR24,
    CURSOR_TYPE_COLOR32,
};

struct qxl_cursor_header {
    uint64_t unique;
    uint16_t type;
    uint16_t width;
    uint16_t height;
    uint16_t hot_spot_x;
    uint16_t hot_spot_y;
};

struct qxl_cursor
{
    struct qxl_cursor_header header;
    uint32_t data_size;
    struct qxl_data_chunk chunk;
};

struct qxl_cursor_cmd {
    union qxl_release_info release_info;
    uint8_t type;
    union {
	struct {
	    struct qxl_point16 position;
	    unsigned char visible;
	    uint64_t shape;
	} set;
	struct {
	    uint16_t length;
	    uint16_t frequency;
	} trail;
	struct qxl_point16 position;
    } u;
    uint8_t device_data[QXL_CURSOR_DEVICE_DATA_SIZE];
};

struct qxl_rom {
    uint32_t magic;
    uint32_t id;
    uint32_t update_id;
    uint32_t compression_level;
    uint32_t log_level;
    uint32_t mode;
    uint32_t modes_offset;
    uint32_t num_io_pages;
    uint32_t pages_offset;
    uint32_t draw_area_offset;
    uint32_t draw_area_size;
    uint32_t ram_header_offset;
    uint32_t mm_clock;
};

struct qxl_ring_header {
    uint32_t num_items;
    uint32_t prod;
    uint32_t notify_on_prod;
    uint32_t cons;
    uint32_t notify_on_cons;
};

#define QXL_LOG_BUF_SIZE 4096

struct qxl_ram_header {
    uint32_t magic;
    uint32_t int_pending;
    uint32_t int_mask;
    unsigned char log_buf[QXL_LOG_BUF_SIZE];
    struct qxl_ring_header  cmd_ring_hdr;
    struct qxl_command	    cmd_ring[32];
    struct qxl_ring_header  cursor_ring_hdr;
    struct qxl_command	    cursor_ring[32];
    struct qxl_ring_header  release_ring_hdr;
    uint64_t		    release_ring[8];
    struct qxl_rect	    update_area;
};

#pragma pack(pop)

typedef struct _qxlScreen qxlScreen;

struct _qxlScreen
{
    /* These are the names QXL uses */
    void *			ram;	/* Video RAM */
    void *			ram_physical;
    void *			vram;	/* Command RAM */
    struct qxl_rom *		rom;    /* Parameter RAM */
    
    struct qxl_ring *		command_ring;
    struct qxl_ring *		cursor_ring;
    struct qxl_ring *		release_ring;
    
    int				num_modes;
    struct qxl_mode *		modes;
    int				io_base;
    int				draw_area_offset;
    int				draw_area_size;

    void *			fb;

    struct qxl_mem *		mem;	/* Context for qxl_alloc/free */
    
    EntityInfoPtr		entity;

    void *			io_pages;
    void *			io_pages_physical;
    
#ifdef XSERVER_LIBPCIACCESS
    struct pci_device *		pci;
#else
    pciVideoPtr			pci;
    PCITAG			pciTag;
#endif

    CreateScreenResourcesProcPtr CreateScreenResources;
    CloseScreenProcPtr		CloseScreen;
    CreateGCProcPtr		CreateGC;
    PaintWindowProcPtr		PaintWindowBackground;
    PaintWindowProcPtr		PaintWindowBorder;
    CopyWindowProcPtr		CopyWindow;
#if 0
    CopyAreaProcPtr		CopyArea;
#endif
    
    DamagePtr			pDamage;
    RegionRec			pendingCopy;
    
    int16_t			cur_x;
    int16_t			cur_y;
    int16_t			hot_x;
    int16_t			hot_y;
    
    ScrnInfoPtr			pScrn;
};

static inline uint64_t
physical_address (qxlScreen *qxl, void *virtual)
{
    return (uint64_t) ((unsigned long)virtual + (((unsigned long)qxl->ram_physical - (unsigned long)qxl->ram)));
}

static inline void *
virtual_address (qxlScreen *qxl, void *physical)
{
    return (void *) ((unsigned long)physical + ((unsigned long)qxl->ram - (unsigned long)qxl->ram_physical));
}

struct qxl_ring;

/*
 * HW cursor
 */
void              qxlCursorInit        (ScreenPtr               pScreen);



/*
 * Rings
 */
struct qxl_ring * qxl_ring_create      (struct qxl_ring_header *header,
					int                     element_size,
					int                     n_elements,
					int                     prod_notify);
void              qxl_ring_push        (struct qxl_ring        *ring,
					const void             *element);
Bool              qxl_ring_pop         (struct qxl_ring        *ring,
					void                   *element);
void              qxl_ring_wait_idle   (struct qxl_ring        *ring);



/*
 * Images
 */
struct qxl_image *qxl_image_create     (qxlScreen              *qxl,
					const uint8_t          *data,
					int                     x,
					int                     y,
					int                     width,
					int                     height,
					int                     stride);
void              qxl_image_destroy    (qxlScreen              *qxl,
					struct qxl_image       *image);
void		  qxl_drop_image_cache (qxlScreen	       *qxl);


/*
 * Malloc
 */
struct qxl_mem *  qxl_mem_create       (void                   *base,
					unsigned long           n_bytes);
void              qxl_mem_dump_stats   (struct qxl_mem         *mem,
					const char             *header);
void *            qxl_alloc            (struct qxl_mem         *mem,
					unsigned long           n_bytes);
void              qxl_free             (struct qxl_mem         *mem,
					void                   *d);
void              qxl_mem_free_all     (struct qxl_mem         *mem);
void *            qxl_allocnf          (qxlScreen              *qxl,
					unsigned long           size);


