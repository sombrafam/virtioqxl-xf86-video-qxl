#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <glib.h>
#include "qxl_mem.h"

#define N_BYTES (4096 * 4096 * 8)

int
main ()
{
    void *base = malloc (N_BYTES);
    int i;
    struct qxl_mem *mem = qxl_mem_create (base, N_BYTES);
    GPtrArray *allocations = g_ptr_array_new ();
    
    assert (base);

    printf ("base: %p\n", base);
    
    for (;;)
    {
	int alloc = rand() % 2;

	if (alloc || allocations->len == 0)
	{
	    int size = 80;
	    
	    void *x = qxl_alloc (mem, size);

	    assert (x != NULL);
	    
	    g_ptr_array_add (allocations, x);

	    printf ("alloc: %d bytes - %p\n", size, x);
	}
	else
	{
	    int n = rand() % allocations->len;
	    void *x = g_ptr_array_remove_index_fast (allocations, n);

	    printf ("free: %p\n", x);
	    
	    qxl_free (mem, x);
	}
    }

    return 0;
}
