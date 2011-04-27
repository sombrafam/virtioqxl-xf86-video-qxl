#ifndef QXL_SPICE_DISPLAY_H
#define QXL_SPICE_DISPLAY_H

#include "qxl.h"
#include <spice.h>

void qxl_add_spice_display_interface(qxl_screen_t *qxl);
/* spice-server to device, now spice-server to xspice */
void qxl_send_events(qxl_screen_t *qxl, int events);

#endif // QXL_SPICE_DISPLAY_H
