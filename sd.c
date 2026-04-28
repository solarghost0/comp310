#include "sd.h"
#include "ide.h"

int sd_readblock(unsigned int lba, char *buffer, unsigned int num_sectors) {
    return ata_lba_read(lba, (unsigned char *)buffer, num_sectors);
}
