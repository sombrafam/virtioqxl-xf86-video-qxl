#include <pthread.h>

#include <spice.h>

#include "qxl.h"
#include "spiceqxl_io_port.h"

/* called from Xorg thread - not worker thread! */
void ioport_write(qxl_screen_t *qxl, uint32_t io_port, uint32_t val)
{
}

