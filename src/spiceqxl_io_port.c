#include <pthread.h>

#include <spice.h>

#include "qxl.h"
#include "spiceqxl_io_port.h"

/* TODO: taken from qemu qxl.c, try to remove dupplication */
#undef SPICE_RING_PROD_ITEM
#define SPICE_RING_PROD_ITEM(r, ret) {                                  \
        typeof(r) start = r;                                            \
        typeof(r) end = r + 1;                                          \
        uint32_t prod = (r)->prod & SPICE_RING_INDEX_MASK(r);           \
        typeof(&(r)->items[prod]) m_item = &(r)->items[prod];           \
        if (!((uint8_t*)m_item >= (uint8_t*)(start) && (uint8_t*)(m_item + 1) <= (uint8_t*)(end))) { \
            abort();                                                    \
        }                                                               \
        ret = &m_item->el;                                              \
    }

#undef SPICE_RING_CONS_ITEM
#define SPICE_RING_CONS_ITEM(r, ret) {                                  \
        typeof(r) start = r;                                            \
        typeof(r) end = r + 1;                                          \
        uint32_t cons = (r)->cons & SPICE_RING_INDEX_MASK(r);           \
        typeof(&(r)->items[cons]) m_item = &(r)->items[cons];           \
        if (!((uint8_t*)m_item >= (uint8_t*)(start) && (uint8_t*)(m_item + 1) <= (uint8_t*)(end))) { \
            abort();                                                    \
        }                                                               \
        ret = &m_item->el;                                              \
    }

static int spiceqxl_io_port_debug_level = 5;

#define dprint(_level, _fmt, ...)                                 \
    do {                                                                \
        if (spiceqxl_io_port_debug_level >= _level) {                                    \
            fprintf(stderr, _fmt, ## __VA_ARGS__);                      \
        }                                                               \
    } while (0)

void init_qxl_ram(qxl_screen_t *qxl)
{
    QXLRam *ram = get_ram_header(qxl);
    uint64_t *item;

    ram->magic       = QXL_RAM_MAGIC;
    ram->int_pending = 0;
    ram->int_mask    = 0;
    SPICE_RING_INIT(&ram->cmd_ring);
    SPICE_RING_INIT(&ram->cursor_ring);
    SPICE_RING_INIT(&ram->release_ring);
    SPICE_RING_PROD_ITEM(&ram->release_ring, item);
    *item = 0;
}

static void qxl_reset_state(qxl_screen_t *qxl)
{
    QXLRam *ram = get_ram_header(qxl);

    assert(SPICE_RING_IS_EMPTY(&ram->cmd_ring));
    assert(SPICE_RING_IS_EMPTY(&ram->cursor_ring));
    qxl->shadow_rom.update_id = 0;
    *qxl->rom = qxl->shadow_rom;
    init_qxl_ram(qxl);
    qxl->num_free_res = 0;
    qxl->last_release = NULL;
    // TODO - dirty ?
    //memset(&qxl->ssd.dirty, 0, sizeof(qxl->ssd.dirty));
}

static void qxl_check_state(qxl_screen_t *qxl)
{
    QXLRam *ram = get_ram_header(qxl);

    assert(SPICE_RING_IS_EMPTY(&ram->cmd_ring));
    assert(SPICE_RING_IS_EMPTY(&ram->cursor_ring));
}

static void qxl_soft_reset(qxl_screen_t *qxl)
{
    dprint(1, "%s:\n", __FUNCTION__);
    qxl_check_state(qxl);
}

static void qxl_reset_surfaces(qxl_screen_t *qxl)
{
    dprint(1, "%s:\n", __FUNCTION__);
    qxl->worker->destroy_surfaces(qxl->worker);
    // TODO - do we have guest_surfaces?
    //memset(&d->guest_surfaces.cmds, 0, sizeof(d->guest_surfaces.cmds));
}

static void qxl_hard_reset(qxl_screen_t *qxl)
{
    dprint(1, "%s: start\n", __FUNCTION__);

    qxl->worker->reset_cursor(qxl->worker);
    qxl->worker->reset_image_cache(qxl->worker);
    qxl_reset_surfaces(qxl);

    qxl_reset_state(qxl);
    qxl_soft_reset(qxl);

    dprint(1, "%s: done\n", __FUNCTION__);
}

static void qxl_create_guest_primary(qxl_screen_t *qxl)
{
    QXLDevSurfaceCreate surface;
    QXLSurfaceCreate *sc = &qxl->guest_primary.surface;

    dprint(1, "%s: %dx%d\n", __FUNCTION__, sc->width, sc->height);

    surface.format     = sc->format;
    surface.height     = sc->height;
    surface.mem        = sc->mem;
    surface.position   = sc->position;
    surface.stride     = sc->stride;
    surface.width      = sc->width;
    surface.type       = sc->type;
    surface.flags      = sc->flags;

    surface.mouse_mode = TRUE;
    surface.group_id   = 0;
    qxl->cmdflags = 0;
    qxl->worker->create_primary_surface(qxl->worker, 0, &surface);
}

static void qxl_destroy_primary(qxl_screen_t *qxl)
{
    dprint(1, "%s\n", __FUNCTION__);

    qxl->worker->destroy_primary_surface(qxl->worker, 0);
}


static void qxl_set_mode(qxl_screen_t *qxl, int modenr)
{
    struct QXLMode *mode = qxl->modes + modenr;
    uint64_t devmem = (uint64_t)qxl->ram;
    QXLSurfaceCreate surface = {
        .width      = mode->x_res,
        .height     = mode->y_res,
        .stride     = -mode->x_res * 4,
        .format     = SPICE_SURFACE_FMT_32_xRGB,
        .flags      = 0,
        .mouse_mode = TRUE,
        .mem        = devmem + qxl->shadow_rom.draw_area_offset,
    };

    dprint(1, "%s: mode %d  [ %d x %d @ %d bpp devmem 0x%lx ]\n", __FUNCTION__,
           modenr, mode->x_res, mode->y_res, mode->bits, devmem);
    qxl_hard_reset(qxl);

    qxl->guest_primary.surface = surface;
    qxl_create_guest_primary(qxl);

    qxl->cmdflags = QXL_COMMAND_FLAG_COMPAT;
#ifdef QXL_COMMAND_FLAG_COMPAT_16BPP /* new in spice 0.6.1 */
    if (mode->bits == 16) {
        qxl->cmdflags |= QXL_COMMAND_FLAG_COMPAT_16BPP;
    }
#endif
    qxl->shadow_rom.mode = modenr;
    qxl->rom->mode = modenr;
}

/* called from Xorg thread - not worker thread! */
void ioport_write(qxl_screen_t *qxl, uint32_t io_port, uint32_t val)
{
    QXLRam *header = get_ram_header(qxl);

    if (!qxl->worker_running) {
        return;
    }

    switch (io_port) {
    case QXL_IO_UPDATE_AREA:
    {
        QXLRect update = *(QXLRect*)&header->update_area;
        qxl->worker->update_area(qxl->worker, header->update_surface,
                                   &update, NULL, 0, 0);
        break;
    }
    case QXL_IO_NOTIFY_CMD:
        qxl->worker->wakeup(qxl->worker);
        break;
    case QXL_IO_NOTIFY_CURSOR:
        qxl->worker->wakeup(qxl->worker);
        break;
    case QXL_IO_UPDATE_IRQ:
        /* qxl_set_irq(d); */
        printf("QXL_IO_UPDATE_IRQ not implemented\n");
        break;
    case QXL_IO_NOTIFY_OOM:
        if (!SPICE_RING_IS_EMPTY(&header->release_ring)) {
            break;
        }
        pthread_yield();
        if (!SPICE_RING_IS_EMPTY(&header->release_ring)) {
            break;
        }
        qxl->worker->oom(qxl->worker);
        break;
    case QXL_IO_SET_MODE:
        dprint(1, "QXL_SET_MODE %d\n", val);
        qxl_set_mode(qxl, val);
        break;
    case QXL_IO_LOG:
        fprintf(stderr, "qxl/guest: %s", header->log_buf);
        break;
    case QXL_IO_RESET:
        dprint(1, "QXL_IO_RESET\n");
        qxl_hard_reset(qxl);
        break;
    case QXL_IO_MEMSLOT_ADD:
        dprint(1, "QXL_IO_MEMSLOT_ADD - should not be called (this is XSpice)\n");
        break;
    case QXL_IO_MEMSLOT_DEL:
        dprint(1, "QXL_IO_MEMSLOT_DEL - should not be called (this is XSpice)\n");
        break;
    case QXL_IO_CREATE_PRIMARY:
        assert(val == 0);
        dprint(1, "QXL_IO_CREATE_PRIMARY\n");
        qxl->guest_primary.surface =
            *(QXLSurfaceCreate*)&header->create_surface;
        qxl_create_guest_primary(qxl);
        break;
    case QXL_IO_DESTROY_PRIMARY:
        assert(val == 0);
        dprint(1, "QXL_IO_DESTROY_PRIMARY\n");
        qxl_destroy_primary(qxl);
        break;
    case QXL_IO_DESTROY_SURFACE_WAIT:
        qxl->worker->destroy_surface_wait(qxl->worker, val);
        break;
    case QXL_IO_DESTROY_ALL_SURFACES:
        qxl->worker->destroy_surfaces(qxl->worker);
        break;
    default:
        fprintf(stderr, "%s: ioport=0x%x, abort()\n", __FUNCTION__, io_port);
        abort();
    }
}

