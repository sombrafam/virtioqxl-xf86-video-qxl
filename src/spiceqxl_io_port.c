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

/* called from Xorg thread - not worker thread! */
void ioport_write(qxl_screen_t *qxl, uint32_t io_port, uint32_t val)
{
}

