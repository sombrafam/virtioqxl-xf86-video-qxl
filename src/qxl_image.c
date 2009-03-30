#include "qxl.h"

#define N_CACHED_IMAGES		4096

static struct qxl_image *image_hash [N_CACHED_IMAGES];



static int
hash_and_copy (const uint8_t *src, int src_stride,
	       uint8_t *dest, int dest_stride,
	       int width, int height)
{
    int i, j;
    int hash = 0;

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

	    hash ^= (s[j] + (hash << 5));
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
    int h;

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

    h = 0; /* Since it doesn't actually work yet */
    
    /* Image */
    image = qxl_allocnf (qxl, sizeof *image);

    image->descriptor.id = h;
    image->descriptor.type = QXL_IMAGE_TYPE_BITMAP;

    image->descriptor.flags = h? QXL_IMAGE_CACHE : 0;
    image->descriptor.width = width;
    image->descriptor.height = height;

    image->u.bitmap.format = QXL_BITMAP_FMT_32BIT;
    image->u.bitmap.flags = QXL_BITMAP_TOP_DOWN;
    image->u.bitmap.x = width;
    image->u.bitmap.y = height;
    image->u.bitmap.stride = width * sizeof (uint32_t);
    image->u.bitmap.palette = 0;
    image->u.bitmap.data = physical_address (qxl, chunk);

    return image;
}

void
qxl_image_destroy (qxlScreen *qxl,
		   struct qxl_image *image)
{
    struct qxl_data_chunk *chunk = virtual_address (
	qxl, (void *)image->u.bitmap.data);

    ErrorF ("Freeing image with id %ld\n", image->descriptor.id);
    
    qxl_free (qxl->mem, chunk);
    qxl_free (qxl->mem, image);
}
