/* An implementation of malloc()/free() for the io pages.
 *
 * The implementation in the Windows drivers is based on
 * the Doug Lea malloc. This one is really simple, at least
 * for now.
 */
#include <stdint.h>
#include <stdlib.h>

struct qxl_mem;

struct qxl_mem *qxl_mem_create (void *base, unsigned long n_bytes);

void *qxl_alloc (struct qxl_mem *mem, unsigned long n_bytes);
void  qxl_free  (struct qxl_mem *mem, void *d);
