#ifndef KYIL_ATA_PIO_H
#define KYIL_ATA_PIO_H
#include <stddef.h>
#include <stdint.h>
/* Host owns the validated/leased channel. Protocol code gets no CPU pointers
 * to registers and never allocates a private physical-memory subsystem. */
typedef struct ata_io {
    void *context;
    uint8_t (*read8)(void *,uint16_t);
    void (*write8)(void *,uint16_t,uint8_t);
    uint16_t (*read16)(void *,uint16_t);
    void (*write16)(void *,uint16_t,uint16_t);
    uint64_t (*now_ns)(void *);
} ata_io_t;
typedef struct ata_pio {
    ata_io_t io;
    uint16_t base,control;
    uint8_t slave,lba48;
    uint64_t sectors;
} ata_pio_t;
int ata_pio_identify(ata_pio_t *disk);
int ata_pio_flush(ata_pio_t *disk);
int ata_pio_transfer(ata_pio_t *disk,uint64_t lba,uint32_t count,void *buffer,int write);
#endif
