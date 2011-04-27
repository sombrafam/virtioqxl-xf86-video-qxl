#ifndef SPICEQXL_IO_PORT_H
#define SPICEQXL_IO_PORT_H

#include "qxl.h"

/* used to initialize the rings before the first reset, avoid a valgrind
 * warning */
void init_qxl_ram(qxl_screen_t *qxl);

#endif // SPICEQXL_IO_PORT_H
