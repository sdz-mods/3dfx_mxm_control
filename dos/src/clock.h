#ifndef DOS_CLOCK_H
#define DOS_CLOCK_H

#include "mxm.h"

typedef struct {
	int available;
	pci_bdf_t gpu;
	u32 bar0;
	u32 pllctrl1;
	char status[64];
} clock_info_t;

void clock_probe(const mxm_card_t *card, clock_info_t *info);
int clock_get_mhz(const clock_info_t *info);
int clock_set_mhz(clock_info_t *info, int mhz);

#endif
