#include "qxl.h"

struct qxl_image *
qxl_image_create (qxlScreen *qxl, const uint8_t *data,
		  int x, int y,
		  int width, int height,
		  int stride)
{
    struct qxl_image *image;
    struct qxl_data_chunk *chunk;
    uint8_t *dest_line;
    const uint8_t *src_line;
    int dest_stride = width * sizeof (uint32_t);
    int i, j;

    data += y * stride + x * sizeof (uint32_t);
    
    /* Chunk */

    /* FIXME: Check integer overflow */
    chunk = qxl_allocnf (qxl, sizeof *chunk + height * dest_stride);

    chunk->data_size = height * dest_stride;
    chunk->prev_chunk = 0;
    chunk->next_chunk = 0;

    for (i = 0; i < height; ++i)
    {
	dest_line = chunk->data + i * dest_stride;
	src_line = data + i * stride;

	for (j = 0; j < width; ++j)
	{
	    uint32_t *s = (uint32_t *)src_line;
	    uint32_t *d = (uint32_t *)dest_line;

	    d[j] = s[j];
	}
    }

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

    return image;
}

void
qxl_image_destroy (qxlScreen *qxl,
		   struct qxl_image *image)
{
    qxl_free (qxl->mem, image);
}
