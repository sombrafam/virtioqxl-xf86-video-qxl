#include <string.h>
#include <assert.h>
#include "qxl.h"

#define N_CACHED_IMAGES		4096

typedef struct image_info_t image_info_t;

typedef struct image_info_t
{
    struct qxl_image *image;
    int width;
    int height;
    int ref_count;
    image_info_t *next;
    unsigned int hash;
};

static image_info_t image_hash [N_CACHED_IMAGES];


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

struct qxl_image *
qxl_image_create (qxlScreen *qxl, const uint8_t *data,
		  int x, int y, int width, int height,
		  int stride)
{
    struct qxl_image *image;
    struct qxl_data_chunk *chunk;
    int dest_stride = width * sizeof (uint32_t);
    unsigned int h;
    image_info_t *info;
    
    data += y * stride + x * sizeof (uint32_t);
    
    /* Chunk */

    /* FIXME: Check integer overflow */
    chunk = qxl_allocnf (qxl, sizeof *chunk + height * dest_stride);

    chunk->data_size = height * dest_stride;
    chunk->prev_chunk = 0;
    chunk->next_chunk = 0;

    h = hash_and_copy (data, stride,
		       chunk->data, dest_stride,
		       width, height);

    ErrorF ("Creating image with hash code %u\n", h);
    
    info = &(image_hash[h % N_CACHED_IMAGES]);
    
    if (h == info->hash				&&
	width == info->width			&&
	height == info->height			&&
	info->ref_count)
    {
	ErrorF ("reusing\n");
	
	qxl_free (qxl->mem, chunk);

	info->ref_count++;
	image = info->image;

	assert (image != NULL);
    }
    else
    {
	if (h == info->hash)
	{
	    ErrorF ("Not reusing because the hash code is wrong\n");
	}
	else if (!info->ref_count)
	{
	    ErrorF ("not reusing because the ref count is wrong\n");
	}
	else if (info->width != width)
	{
	    ErrorF ("not reusing because the width is wrong (%d != %d)",
		    width, info->width);
	}
	else if (info->height != height)
	{
	    ErrorF ("not reusing because the width is wrong\n");
	}
	
	/* Image */
	image = qxl_allocnf (qxl, sizeof *image);

#if 0
	ErrorF ("allocated %p\n", image);
#endif
	
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

#if 0
	ErrorF ("Inserting image %d\n", h);
#endif

	if (info->image)
	{
	    ErrorF ("image: %p ref count %d\n", info->image, info->ref_count);
	    
	    ErrorF ("Collision at %u (%u %% %d == %u)\n", h % N_CACHED_IMAGES, h, N_CACHED_IMAGES, h % N_CACHED_IMAGES);
	    ErrorF ("hash: %u %u  width: %x %x height %x %x image %p %p\n",
		    h, info->hash,
		    width, info->width,
		    height, info->height,
		    image, info->image);
	}
	else if (h % N_CACHED_IMAGES != 0)
	{
	    info->image = image;
	    info->width = width;
	    info->height = height;
	    info->hash = h;
	    info->ref_count = 1;

	    image->descriptor.id = h;
	    image->descriptor.flags = QXL_IMAGE_CACHE;

	    ErrorF ("cached with ref count %d\n", info->ref_count);
	}
	else
	{
	    ErrorF (" %d %% %d == 0\n", h, N_CACHED_IMAGES, h % N_CACHED_IMAGES);
	}
    }

    if (image->descriptor.type != QXL_IMAGE_TYPE_BITMAP) {
	ErrorF ("using existing image %p (%d x %d) id: %lu type: %d\n",
		image, width, height, image->descriptor.id, image->descriptor.type);
    }
	

    return image;
}

void
qxl_image_destroy (qxlScreen *qxl,
		   struct qxl_image *image)
{
    image_info_t *info;

    ErrorF ("Destroying %p\n", image);
    
    struct qxl_data_chunk *chunk = virtual_address (
	qxl, (void *)image->u.bitmap.data);

    info = &image_hash[image->descriptor.id % N_CACHED_IMAGES];

    if (info->image == image && --info->ref_count == 0)
    {
	info->image = NULL;
	info->width = 0xdeadbeef;
	info->height = 0xdeadbeef;
	
	qxl_free (qxl->mem, chunk);
	qxl_free (qxl->mem, image);
    }
    else if (info->image != image)
    {
	qxl_free (qxl->mem, chunk);
	qxl_free (qxl->mem, image);
    }	
}

void
qxl_drop_image_cache (qxlScreen *qxl)
{
    memset (image_hash, 0, sizeof image_hash);
}
