#include "ide.h"
#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void insw(uint16_t port, void *addr, uint32_t count) {
    asm volatile("rep insw" : "+D"(addr), "+c"(count) : "d"(port) : "memory");
}

int ata_lba_read(unsigned int lba, unsigned char *buffer, unsigned int num_sectors) {
    for (unsigned int i = 0; i < num_sectors; i++) {
        unsigned int sector = lba + i;
        outb(0x1F6, 0xE0 | ((sector >> 24) & 0x0F));
        outb(0x1F2, 1);
        outb(0x1F3, sector & 0xFF);
        outb(0x1F4, (sector >> 8) & 0xFF);
        outb(0x1F5, (sector >> 16) & 0xFF);
        outb(0x1F7, 0x20); // READ SECTORS command

        // Wait for drive to be ready
        while (!(inb(0x1F7) & 0x08));

        insw(0x1F0, buffer + (i * 512), 256); // 256 words = 512 bytes
    }
    return 0;
}
