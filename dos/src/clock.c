#include <stdio.h>
#include <string.h>

#include "clock.h"

#define TDFX_VENDOR 0x121a
#define AVENGER_DEVICE 0x0005
#define NAPALM_DEVICE 0x0009
#define PLL_REF_SCALED 1431818UL

extern int __cdecl cpu_in_v86(void);
extern u32 __cdecl mmio_read32(u32 address);
extern void __cdecl mmio_write32(u32 address, u32 value);

static u32 mhz_to_pll(int mhz)
{
	u32 n;

	if (mhz < MXM_CLOCK_MIN_MHZ)
		mhz = MXM_CLOCK_MIN_MHZ;
	if (mhz > MXM_CLOCK_MAX_MHZ)
		mhz = MXM_CLOCK_MAX_MHZ;
	n = (((u32)mhz * 600000UL + PLL_REF_SCALED / 2) /
	     PLL_REF_SCALED) - 2;
	if (n > 255)
		n = 255;
	return (n << 8) | (1UL << 2) | 1UL;
}

static int pll_to_mhz(u32 pll)
{
	u32 n = (pll >> 8) & 0xff;
	u32 m = (pll >> 2) & 0x3f;
	u32 k = pll & 0x03;
	u32 divisor = (m + 2) * (1UL << k);

	return (int)((PLL_REF_SCALED * (n + 2) + divisor * 50000UL) /
		     (divisor * 100000UL));
}

void clock_probe(const mxm_card_t *card, clock_info_t *info)
{
	u16 device;

	memset(info, 0, sizeof(*info));
	device = card->type == MXM_CARD_M4800 ? NAPALM_DEVICE : AVENGER_DEVICE;
	if (pci_find_device(TDFX_VENDOR, device, &info->gpu)) {
		strcpy(info->status, "3dfx GPU PCI function was not found.");
		return;
	}
	if (cpu_in_v86()) {
		strcpy(info->status, "Clock unavailable under a V86 memory manager.");
		return;
	}

	info->bar0 = pci_read32(info->gpu, 0x10) & 0xfffffff0UL;
	if (!info->bar0) {
		strcpy(info->status, "3dfx register BAR is not assigned.");
		return;
	}
	info->pllctrl1 = mmio_read32(info->bar0 + MXM_PLLCTRL1);
	info->available = 1;
	sprintf(info->status, "Direct MMIO, PLLCTRL1 %08lX", info->pllctrl1);
}

int clock_get_mhz(const clock_info_t *info)
{
	if (!info->available)
		return MXM_CLOCK_DEFAULT_MHZ;
	return pll_to_mhz(info->pllctrl1);
}

int clock_set_mhz(clock_info_t *info, int mhz)
{
	u32 pll;

	if (!info->available)
		return -1;
	pll = mhz_to_pll(mhz);
	mmio_write32(info->bar0 + MXM_PLLCTRL1, pll);
	info->pllctrl1 = mmio_read32(info->bar0 + MXM_PLLCTRL1);
	sprintf(info->status, "Direct MMIO, PLLCTRL1 %08lX", info->pllctrl1);
	return 0;
}
