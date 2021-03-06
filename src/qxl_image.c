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
#include "murmurhash3.h"

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
	       int bytes_per_pixel, int width, int height,
	       uint32_t hash)
{
    int i;
  
    for (i = 0; i < height; ++i)
    {
	const uint8_t *src_line = src + i * src_stride;
	uint8_t *dest_line = dest + i * dest_stride;
	int n_bytes = width * bytes_per_pixel;

	if (dest)
	    memcpy (dest_line, src_line, n_bytes);

	MurmurHash3_x86_32 (src_line, n_bytes, hash, &hash);
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

#define MAX(a,b)  (((a) > (b))? (a) : (b))
#define MIN(a,b)  (((a) < (b))? (a) : (b))

struct QXLImage *
qxl_image_create (qxl_screen_t *qxl, const uint8_t *data,
		  int x, int y, int width, int height,
		  int stride, int Bpp, Bool fallback)
{
	uint32_t hash;
	image_info_t *info;
	struct QXLImage *image;
	struct QXLDataChunk *head;
	struct QXLDataChunk *tail;
	int dest_stride = width * Bpp;
	int h;

	data += y * stride + x * Bpp;

#if 0
	ErrorF ("Must create new image of size %d %d\n", width, height);
#endif
	
	/* Chunk */

	/* FIXME: Check integer overflow */

	head = tail = NULL;

	hash = 0;
	h = height;
	while (h)
	{
	    int chunk_size = MAX (512 * 512, dest_stride);
	    int n_lines = MIN ((chunk_size / dest_stride), h);
	    QXLDataChunk *chunk =
		qxl_allocnf (qxl, sizeof *chunk + n_lines * dest_stride);

	    chunk->data_size = n_lines * dest_stride;
	    hash = hash_and_copy (data, stride,
				  chunk->data, dest_stride,
				  Bpp, width, n_lines, hash);
	    
	    if (tail)
	    {
		tail->next_chunk = physical_address (qxl, chunk, qxl->main_mem_slot);
		chunk->prev_chunk = physical_address (qxl, tail, qxl->main_mem_slot);
		chunk->next_chunk = 0;
		
		tail = chunk;
	    }
	    else
	    {
		head = tail = chunk;
		chunk->next_chunk = 0;
		chunk->prev_chunk = 0;
	    }

	    data += n_lines * stride;
	    h -= n_lines;
	}

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
	{
	    abort();
	}

	image->bitmap.flags = SPICE_BITMAP_FLAGS_TOP_DOWN;
	image->bitmap.x = width;
	image->bitmap.y = height;
	image->bitmap.stride = width * Bpp;
	image->bitmap.palette = 0;
	image->bitmap.data = physical_address (qxl, head, qxl->main_mem_slot);

#if 0
	ErrorF ("%p has size %d %d\n", image, width, height);
#endif
	
	/* Add to hash table if caching is enabled */
	if ((fallback && qxl->enable_fallback_cache)	||
	    (!fallback && qxl->enable_image_cache))
	{
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
	}

	return image;
}

void
qxl_image_destroy (qxl_screen_t *qxl,
		   struct QXLImage *image)
{
    image_info_t *info;
    uint64_t chunk;

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

    
    chunk = image->bitmap.data;
    while (chunk)
    {
	struct QXLDataChunk *virtual;

	virtual = virtual_address (qxl, u64_to_pointer (chunk), qxl->main_mem_slot);

	chunk = virtual->next_chunk;

	qxl_free (qxl->mem, virtual);
    }
    
    qxl_free (qxl->mem, image);
}

void
qxl_drop_image_cache (qxl_screen_t *qxl)
{
    memset (image_table, 0, HASH_SIZE * sizeof (image_info_t *));
}
