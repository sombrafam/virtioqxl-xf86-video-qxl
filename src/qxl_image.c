#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include "qxl.h"

typedef struct image_info_t image_info_t;

struct image_info_t
{
    struct qxl_image *image;
    int ref_count;
};

#define INITIAL_SIZE	4096

#define TOMBSTONE	((struct qxl_image *)(unsigned long)(-1))

static int	     n_images		= 0;
static int	     n_allocated	= 0;	
static image_info_t *image_table	= NULL;

static Bool
grow_or_shrink_table (int n_images_new)
{
    int new_size;

    if (!image_table)
    {
	new_size = INITIAL_SIZE;
    }
    else if (3 * n_images_new > n_allocated)
    {
	new_size = n_allocated * 2;
    }
    else if (9 * n_images_new < n_allocated)
    {
	new_size = n_allocated >> 2;
    }
    else
    {
	new_size = n_allocated;
    }

    if (new_size != n_allocated)
    {
	image_info_t *new_table = calloc (new_size, sizeof (image_info_t));
	int i;

	if (!new_table)
	    return FALSE;

	for (i = 0; i < n_allocated; ++i)
	{
	    int new_pos = i % new_size;

	    new_table[new_pos] = image_table[i];
	}

	free (image_table);

	image_table = new_table;
	n_allocated = new_size;
    }

    return TRUE;
}

static unsigned int
hash_and_copy (const uint8_t *src, int src_stride,
	       uint8_t *dest, int dest_stride,
	       int width, int height)
{
    int i, j;
    unsigned int hash = 0;

    for (i = 0; i < height; ++i)
    {
	const uint8_t *src_line = src + i * src_stride;
	uint8_t *dest_line = dest + i * dest_stride;
	
	for (j = 0; j < width; ++j)
	{
	    uint32_t *s = (uint32_t *)src_line;
	    uint32_t *d = (uint32_t *)dest_line;

	    if (dest)
		d[j] = s[j];

	    hash = (hash << 5) - hash + s[i] + 0xab;
	}
    }

    return hash;
}

static image_info_t *
lookup_image_info (unsigned int hash,
		   int width,
		   int height)
{
    struct image_info_t *info = &image_table[hash % n_allocated];
    
    while (info->image != NULL)
    {
	struct qxl_image *image = info->image;

	if (image != TOMBSTONE				&&
	    image->descriptor.id == hash		&&
	    image->descriptor.width == width		&&
	    image->descriptor.height == height)
	{
	    return info;
	}

	if (++info == image_table + n_allocated)
	    info = image_table;
    }

    return NULL;
}

static image_info_t *
find_available_image_info (unsigned int hash)
{
    struct image_info_t *info = &image_table[hash % n_allocated];
    
    while (info->image != NULL)
    {
	if (!info->image || info->image == TOMBSTONE)
	    return info;
	
	if (++info == image_table + n_allocated)
	    info = image_table;
    }

    return NULL;
}

struct qxl_image *
qxl_image_create (qxlScreen *qxl, const uint8_t *data,
		  int x, int y, int width, int height,
		  int stride)
{
    unsigned int hash = hash_and_copy (data, stride, NULL, -1, width, height);
    image_info_t *info;

    data += y * stride + x * sizeof (uint32_t);

    info = lookup_image_info (hash, width, height);
    if (info)
    {
	info->ref_count++;

	return info->image;
    }
    else
    {
	struct qxl_image *image;
	struct qxl_data_chunk *chunk;
	int dest_stride = width * sizeof (uint32_t);
	
	/* Chunk */
	
	/* FIXME: Check integer overflow */
	chunk = qxl_allocnf (qxl, sizeof *chunk + height * dest_stride);
	
	chunk->data_size = height * dest_stride;
	chunk->prev_chunk = 0;
	chunk->next_chunk = 0;
	
	hash_and_copy (data, stride,
		       chunk->data, dest_stride,
		       width, height);

		
	/* Image */
	image = qxl_allocnf (qxl, sizeof *image);

	image->descriptor.id = 0;
	image->descriptor.type = QXL_IMAGE_TYPE_BITMAP;
	
	image->descriptor.flags = 0;
	image->descriptor.width = width;
	image->descriptor.height = height;
	
	image->u.bitmap.format = QXL_BITMAP_FMT_32BIT;
	image->u.bitmap.flags = QXL_BITMAP_TOP_DOWN;
	image->u.bitmap.x = width;
	image->u.bitmap.y = height;
	image->u.bitmap.stride = width * sizeof (uint32_t);
	image->u.bitmap.palette = 0;
	image->u.bitmap.data = physical_address (qxl, chunk);

	/* Add to hash table */
	if (grow_or_shrink_table (n_images + 1))
	{
	    image_info_t *info = find_available_image_info (hash);

	    if (info)
	    {
		info->image = image;
		info->ref_count = 1;
		
		image->descriptor.id = hash;
		image->descriptor.flags = QXL_IMAGE_CACHE;
	    }
	}

	return image;
    }
}

void
qxl_image_destroy (qxlScreen *qxl,
		   struct qxl_image *image)
{
    struct qxl_data_chunk *chunk;
    image_info_t *info;

    ErrorF ("Destroying %p\n", image);
    
    chunk = virtual_address (qxl, (void *)image->u.bitmap.data);
    
    info = lookup_image_info (image->descriptor.id,
			      image->descriptor.width,
			      image->descriptor.height);
    
    if (info->image == image)
    {
	if (--info->ref_count != 0)
	    return;

	info->image = TOMBSTONE;

	grow_or_shrink_table (n_images - 1);
	n_images--;
    }

    qxl_free (qxl->mem, chunk);
    qxl_free (qxl->mem, image);
}

void
qxl_drop_image_cache (qxlScreen *qxl)
{
    memset (image_table, 0, n_allocated * sizeof (image_info_t));

    n_images = 0;
}
