/*
 * Copyright 2009, 2010 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */
/** \file QXLImage.c
 * \author Søren Sandmann <sandmann@redhat.com>
 */

#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include "qxl.h"
#include "lookup3.h"

typedef struct image_info_t image_info_t;

struct image_info_t
{
    struct QXLImage *image;
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
	struct QXLImage *image = info->image;

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

#if 0
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
#endif

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

struct QXLImage *
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
	    struct QXLDataChunk *chunk;
	    const uint8_t *src_line = data + i * stride;
	    uint32_t *dest_line;
		
	    chunk = virtual_address (qxl, u64_to_pointer (info->image->bitmap.data), qxl->main_mem_slot);
	    
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
	struct QXLImage *image;
	struct QXLDataChunk *chunk;
	int dest_stride = width * Bpp;

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
	image->descriptor.type = SPICE_IMAGE_TYPE_BITMAP;
	
	image->descriptor.flags = 0;
	image->descriptor.width = width;
	image->descriptor.height = height;

	if (Bpp == 2)
	{
	    image->bitmap.format = SPICE_BITMAP_FMT_16BIT;
	}
	else if (Bpp == 1)
	{
	    image->bitmap.format = SPICE_BITMAP_FMT_8BIT;
	}
	else if (Bpp == 4)
	{
	    image->bitmap.format = SPICE_BITMAP_FMT_32BIT;
	}
	else
	  abort();

	image->bitmap.flags = SPICE_BITMAP_FLAGS_TOP_DOWN;
	image->bitmap.x = width;
	image->bitmap.y = height;
	image->bitmap.stride = width * Bpp;
	image->bitmap.palette = 0;
	image->bitmap.data = physical_address (qxl, chunk, qxl->main_mem_slot);

#if 0
	ErrorF ("%p has size %d %d\n", image, width, height);
#endif
	
#if 0
	/* Add to hash table */
	if ((info = insert_image_info (hash)))
	{
	    info->image = image;
	    info->ref_count = 1;

	    image->descriptor.id = hash;
	    image->descriptor.flags = SPICE_IMAGE_CACHE;

#if 0
	    ErrorF ("added with hash %u\n", hash);
#endif
	}
#endif

	return image;
    }
}

void
qxl_image_destroy (qxl_screen_t *qxl,
		   struct QXLImage *image)
{
    struct QXLDataChunk *chunk;
    image_info_t *info;

    chunk = virtual_address (qxl, u64_to_pointer (image->bitmap.data), qxl->main_mem_slot);
    
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
