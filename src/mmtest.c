#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <glib.h>
#include "qxl.h"

#define N_BYTES (4096 * 4096 * 8)
#define g_print(...)
#define printf(...)

static void
dump_allocations (GPtrArray *array, const char *header)
{
    int i;

    g_print ("%s: [ ");
    for (i = 0; i < array->len; ++i)
    {
	g_print ("%p (%d), ", array->pdata[i], *((unsigned long *)array->pdata[i] - 1));
    }
    g_print (" ]\n");
    
}

int
main ()
{
    void *base = malloc (N_BYTES);
    struct qxl_mem *mem = qxl_mem_create (base, N_BYTES);
    GPtrArray *allocations = g_ptr_array_new ();
    
    assert (base);

    printf ("base: %p\n", base);
    
    for (;;)
    {
	int alloc = rand() % 2;

	if (alloc || allocations->len == 0)
	{
	    int size = 4 * (rand () % 1024);
	    
	    void *x = qxl_alloc (mem, size);

	    assert (x != NULL);
	    
	    g_ptr_array_add (allocations, x);

	    printf ("alloc: %d bytes - %p (%d allocations total)\n", size, x, allocations->len);
	    dump_allocations (allocations, "after alloc");

	}
	else
	{
	    printf ("len: %d\n", allocations->len);
	    
	    int n = rand() % allocations->len;
	    void *x = g_ptr_array_remove_index_fast (allocations, n);

	    printf ("free: %p (number %d)\n", x, n);
	    
	    qxl_free (mem, x);

	    dump_allocations (allocations, "after free");
	}
    }

    return 0;
}
