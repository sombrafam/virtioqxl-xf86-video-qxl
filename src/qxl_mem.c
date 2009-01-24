#include "qxl_mem.h"

struct block
{
    unsigned long n_bytes;
    
    union
    {
	struct
	{
	    struct block *next;
	} unused;

	struct
	{
	    uint8_t data[0];
	} used;
    } u;
};

struct qxl_mem
{
    struct block *unused;
};

struct qxl_mem *
qxl_mem_create (void *base, unsigned long n_bytes)
{
    struct qxl_mem *mem = NULL;

    mem = xcalloc (sizeof (*mem), 1);
    if (!mem)
	goto out;

    mem->unused = (struct block *)base;
    mem->unused->n_bytes = n_bytes;
    mem->unused->u.unused.next = NULL;
    
out:
    return mem;
}

void *
qxl_alloc (struct qxl_mem *mem, unsigned long n_bytes)
{
    struct block *b, *prev;
    
    /* Simply pretend the user asked to allocate the header as well. Then
     * we can mostly ignore the difference between blocks and allocations
     */
    n_bytes += sizeof (unsigned long);
    
    if (n_bytes < sizeof (struct block))
	n_bytes = sizeof (struct block);

    prev = NULL;
    for (b = mem->unused; b != NULL; prev = b, b = b->u.unused.next)
    {
	if (b->n_bytes >= n_bytes)
	{
	    struct block *new_block;

	    if (b->n_bytes > n_bytes)
	    {
		new_block = (void *)b + n_bytes;
		new_block->n_bytes = b->n_bytes - n_bytes;

		if (prev)
		{
 		    new_block->u.unused.next = prev->u.unused.next;
		    prev->u.unused.next = new_block;
		}
		else
		{
		    assert (mem->unused == NULL);
		    
		    mem->unused = new_block;
		}
	    }

	    b->n_bytes = n_bytes;
	    return (void *)b->u.used.data;
	}
    }
}

/* Finds the unused block before and the unused block after @data. Both
 * before and after can be NULL if data is before the first or after the
 * last unused block.
 */
static void
find_neighbours (struct qxl_mem *mem, void *data,
		 struct block **before, struct block **after)
{
    struct block *b;
    *before = NULL;
    *after = NULL;
    
    for (b = mem->unused; b != NULL; b = b->u.unused.next)
    {
	if ((void *)b < data)
	    *before = b;
	
	if ((void *)b > data)
	{
	    *after = b;
	    break;
	}
    }

    if (*before)
	assert ((*before)->u.unused.next == *after);
}

void
qxl_free (struct qxl_mem *mem, void *d)
{
    struct block *b = d - sizeof (unsigned long);
    struct block *before, *after;

    find_neighbours (mem, (void *)b, &before, &after);

    if (before)
    {
	if ((void *)before + before->n_bytes == b)
	{
	    /* Merge before and b */
	    before->n_bytes += b->n_bytes;
	    b = before;
	}
	else
	{
	    before->u.unused.next = b;
	}
    }
    else
    {
	mem->unused = b;
    }
    
    if (after)
    {
	if ((void *)b + b->n_bytes == after)
	{
	    b->n_bytes += after->n_bytes;
	}
	else
	{
	    b->u.unused.next = after;
 	}
    }
    else
    {
	b->u.unused.next = NULL;
    }
}
