#ifndef __SD_H__
#define __SD_H__

int sd_readblock(unsigned int lba, char *buffer, unsigned int num_sectors);

#endif
