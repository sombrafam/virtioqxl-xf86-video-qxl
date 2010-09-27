#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include "qxl.h"
#include "lookup3.h"

typedef struct image_info_t image_info_t;

struct image_info_t
{
    struct qxl_image *image;
    int ref_count;
    image_info_t *next;
};

#define HASH_SIZE 4096
static image_info_t *image_table[HASH_SIZE];

static unsigned int
hash_and_copy (const uint8_t *src, int src_stride,
	       uint8_t *dest, int dest_stride,
	       int bytes_per_pixel, int width, int height)
{
    unsigned int hash = 0;
    int i;
  
    for (i = 0; i < height; ++i)
    {
	const uint8_t *src_line = src + i * src_stride;
	uint8_t *dest_line = dest + i * dest_stride;
	int n_bytes = width * bytes_per_pixel;

	if (dest)
	    memcpy (dest_line, src_line, n_bytes);

	hash = hashlittle (src_line, n_bytes, hash);
    }

    return hash;
}

static image_info_t *
lookup_image_info (unsigned int hash,
		   int width,
		   int height)
{
    struct image_info_t *info = image_table[hash % HASH_SIZE];

    while (info)
    {
	struct qxl_image *image = info->image;

	if (image->descriptor.id == hash		&&
	    image->descriptor.width == width		&&
	    image->descriptor.height == height)
	{
	    return info;
	}

	info = info->next;
    }

#if 0
    ErrorF ("lookup of %u failed\n", hash);
#endif
    
    return NULL;
}

static image_info_t *
insert_image_info (unsigned int hash)
{
    struct image_info_t *info = malloc (sizeof (image_info_t));

    if (!info)
	return NULL;

    info->next = image_table[hash % HASH_SIZE];
    image_table[hash % HASH_SIZE] = info;
    
    return info;
}

static void
remove_image_info (image_info_t *info)
{
    struct image_info_t **location = &image_table[info->image->descriptor.id % HASH_SIZE];

    while (*location && (*location) != info)
	location = &((*location)->next);

    if (*location)
	*location = info->next;

    free (info);
}

struct qxl_image *
qxl_image_create (qxl_screen_t *qxl, const uint8_t *data,
		  int x, int y, int width, int height,
		  int stride, int Bpp)
{
    unsigned int hash;
    image_info_t *info;

    data += y * stride + x * Bpp;

    hash = hash_and_copy (data, stride, NULL, -1, Bpp, width, height);

    info = lookup_image_info (hash, width, height);
    if (info)
    {
	int i, j;
	
#if 0
	ErrorF ("reusing image %p with hash %u (%d x %d)\n", info->image, hash, width, height);
#endif
	
	info->ref_count++;

	for (i = 0; i < height; ++i)
	{
	    struct qxl_data_chunk *chunk;
	    const uint8_t *src_line = data + i * stride;
	    uint32_t *dest_line;
		
	    chunk = virtual_address (qxl, u64_to_pointer (info->image->u.bitmap.data), qxl->main_mem_slot);
	    
	    dest_line = (uint32_t *)chunk->data + width * i;

	    for (j = 0; j < width; ++j)
	    {
		uint32_t *s = (uint32_t *)src_line;
		uint32_t *d = (uint32_t *)dest_line;
		
		if (d[j] != s[j])
		{
#if 0
		    ErrorF ("bad collision at (%d, %d)! %d != %d\n", j, i, s[j], d[j]);
#endif
		    goto out;
		}
	    }
	}
    out:
	return info->image;
    }
    else
    {
	struct qxl_image *image;
	struct qxl_data_chunk *chunk;
	int dest_stride = width * Bpp;
	image_info_t *info;

#if 0
	ErrorF ("Must create new image of size %d %d\n", width, height);
#endif
	
	/* Chunk */
	
	/* FIXME: Check integer overflow */
	chunk = qxl_allocnf (qxl, sizeof *chunk + height * dest_stride);
	
	chunk->data_size = height * dest_stride;
	chunk->prev_chunk = 0;
	chunk->next_chunk = 0;
	
	hash_and_copy (data, stride,
		       chunk->data, dest_stride,
		       Bpp, width, height);

	/* Image */
	image = qxl_allocnf (qxl, sizeof *image);

	image->descriptor.id = 0;
	image->descriptor.type = QXL_IMAGE_TYPE_BITMAP;
	
	image->descriptor.flags = 0;
	image->descriptor.width = width;
	image->descriptor.height = height;

	if (Bpp == 2)
	{
	    image->u.bitmap.format = QXL_BITMAP_FMT_16BIT;
	}
	else if (Bpp == 1)
	{
	    image->u.bitmap.format = QXL_BITMAP_FMT_8BIT;
	}
	else if (Bpp == 4)
	{
	    image->u.bitmap.format = QXL_BITMAP_FMT_32BIT;
	}
	else
	  abort();

	image->u.bitmap.flags = QXL_BITMAP_TOP_DOWN;
	image->u.bitmap.x = width;
	image->u.bitmap.y = height;
	image->u.bitmap.stride = width * Bpp;
	image->u.bitmap.palette = 0;
	image->u.bitmap.data = physical_address (qxl, chunk, qxl->main_mem_slot);

#if 0
	ErrorF ("%p has size %d %d\n", image, width, height);
#endif
	
	/* Add to hash table */
	if ((info = insert_image_info (hash)))
	{
	    info->image = image;
	    info->ref_count = 1;

	    image->descriptor.id = hash;
	    image->descriptor.flags = QXL_IMAGE_CACHE;

#if 0
	    ErrorF ("added with hash %u\n", hash);
#endif
	}

	return image;
    }
}

void
qxl_image_destroy (qxl_screen_t *qxl,
		   struct qxl_image *image)
{
    struct qxl_data_chunk *chunk;
    image_info_t *info;

    chunk = virtual_address (qxl, u64_to_pointer (image->u.bitmap.data), qxl->main_mem_slot);
    
    info = lookup_image_info (image->descriptor.id,
			      image->descriptor.width,
			      image->descriptor.height);

    if (info && info->image == image)
    {
	--info->ref_count;

	if (info->ref_count != 0)
	    return;

#if 0
	ErrorF ("removed %p from hash table\n", info->image);
#endif
	
	remove_image_info (info);
    }

    qxl_free (qxl->mem, chunk);
    qxl_free (qxl->mem, image);
}

void
qxl_drop_image_cache (qxl_screen_t *qxl)
{
    memset (image_table, 0, HASH_SIZE * sizeof (image_info_t *));
}
