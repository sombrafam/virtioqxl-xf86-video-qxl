#ifndef SPICEQXL_DRIVER_H
#define SPICEQXL_DRIVER_H 1

#define VGA_RAM_SIZE (16 * 1024 * 1024)

#define RAM_SIZE (128L<<20) // must be >VGA_RAM_SIZE
#define VRAM_SIZE (128L<<20)
#define ROM_SIZE (1<<20) // TODO - put correct size

void init_qxl_rom(qxl_screen_t* qxl, uint32_t rom_size);
#endif /* SPICEQXL_DRIVER_H */
