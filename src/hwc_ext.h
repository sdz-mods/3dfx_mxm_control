#ifndef HWC_EXT_H
#define HWC_EXT_H

#include <windows.h>

typedef struct {
	int available;
	UINT escape_id;
	DWORD driver_major;
	DWORD driver_minor;
	DWORD vendor_id;
	DWORD device_id;
	DWORD fb_ram;
	DWORD chip_rev;
	DWORD num_base_addrs;
	DWORD base0;
	DWORD pllctrl1;
	char status[160];
} hwc_ext_info_t;

int hwc_ext_probe(HWND hwnd, hwc_ext_info_t *info);
int hwc_ext_pll_to_mhz(DWORD pll);
DWORD hwc_ext_mhz_to_pll(int mhz);
int hwc_ext_set_clock_mhz(hwc_ext_info_t *info, int mhz, char *err, int err_len);

#endif
