#ifndef QXL_MAIN_LOOP_H
#define QXL_MAIN_LOOP_H

#include "qxl.h"
#include <spice.h>

SpiceCoreInterface *basic_event_loop_init(void);
void basic_event_loop_mainloop(void);

#endif // QXL_MAIN_LOOP_H
