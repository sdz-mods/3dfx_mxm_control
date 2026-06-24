#include <windows.h>
#include <string.h>

#include "hwc_ext.h"
#include "mxm_protocol.h"
#include "pci.h"

#define EXT_HWC_NEW 0x3df3

#define HWCEXT_GETDRIVERVERSION 0x0
#define HWCEXT_GETDEVICECONFIG  0x2
#define HWCEXT_GETLINEARADDR    0x3

#define HWCEXT_MAX_BASEADDR 9

#define PLL_REF_MHZ 14.31818
typedef struct {
	DWORD major;
	DWORD minor;
} hwc_driver_version_res_t;

typedef struct {
	HDC dc;
	DWORD dev_no;
} hwc_device_config_req_t;

typedef struct {
	DWORD dev_num;
	DWORD vendor_id;
	DWORD device_id;
	DWORD fb_ram;
	DWORD chip_rev;
	DWORD pci_stride;
	DWORD hw_stride;
	DWORD tile_mark;
	DWORD is_master;
	DWORD num_chips;
} hwc_device_config_res_t;

typedef struct {
	DWORD dev_num;
	DWORD process_handle;
} hwc_linear_addr_req_t;

typedef struct {
	DWORD num_base_addrs;
	DWORD base_addresses[HWCEXT_MAX_BASEADDR];
} hwc_linear_addr_res_t;

typedef struct {
	DWORD context_id;
	DWORD which;
	union {
		hwc_device_config_req_t device_config;
		hwc_linear_addr_req_t linear_addr;
		BYTE pad[128];
	} opt;
} hwc_request_t;

typedef struct {
	LONG res_status;
	union {
		hwc_driver_version_res_t driver_version;
		hwc_device_config_res_t device_config;
		hwc_linear_addr_res_t linear_addr;
		BYTE pad[256];
	} opt;
} hwc_result_t;

static int call_escape(HDC dc, UINT escape_id, hwc_request_t *req, hwc_result_t *res)
{
	int ret;

	memset(res, 0, sizeof(*res));
	ret = ExtEscape(dc, escape_id, sizeof(*req), (LPCSTR)req,
			sizeof(*res), (LPSTR)res);
	return ret > 0;
}

static double pll_to_mhz_double(DWORD pll)
{
	DWORD n = (pll >> 8) & 0xff;
	DWORD m = (pll >> 2) & 0x3f;
	DWORD k = pll & 0x03;

	return PLL_REF_MHZ * (double)(n + 2) /
		((double)(m + 2) * (double)(1UL << k));
}

int hwc_ext_pll_to_mhz(DWORD pll)
{
	double mhz = pll_to_mhz_double(pll);

	return (int)(mhz + 0.5);
}

DWORD hwc_ext_mhz_to_pll(int mhz)
{
	int n;

	if (mhz < MXM_CLOCK_MIN_MHZ)
		mhz = MXM_CLOCK_MIN_MHZ;
	if (mhz > MXM_CLOCK_MAX_MHZ)
		mhz = MXM_CLOCK_MAX_MHZ;

	/*
	 * Match the 3dfx H4 driver PLL table shape used for this range:
	 * M = 1, K = 1, N rounded for the requested MHz.
	 *
	 * A full "closest mathematical PLL" search can choose exotic divisors
	 * such as M = 0. Those are numerically close, but not what the 3dfx
	 * table uses and they can corrupt/crash the card.
	 */
	n = (int)(((double)mhz * 6.0 / PLL_REF_MHZ) - 2.0 + 0.5);
	if (n < 0)
		n = 0;
	if (n > 255)
		n = 255;

	return ((DWORD)n << 8) | (1UL << 2) | 1UL;
}

int hwc_ext_set_clock_mhz(hwc_ext_info_t *info, int mhz, char *err, int err_len)
{
	volatile DWORD *pll_reg;
	DWORD pll;

	if (!info || !info->available || !info->base0) {
		if (err && err_len)
			lstrcpynA(err, "3dfx driver interface is not available.", err_len);
		return 1;
	}

	pll = hwc_ext_mhz_to_pll(mhz);
	pll_reg = (volatile DWORD *)(info->base0 + MXM_PLLCTRL1);
	if (IsBadWritePtr((void *)pll_reg, sizeof(DWORD))) {
		if (err && err_len)
			lstrcpynA(err, "3dfx register mapping is not writable.", err_len);
		return 1;
	}

	*pll_reg = pll;
	info->pllctrl1 = *pll_reg;
	wsprintfA(info->status,
		  "3dfx drv: EXT %04X dev %04X:%04X PLL %08X",
		  info->escape_id, info->vendor_id, info->device_id,
		  info->pllctrl1);
	return 0;
}

static int probe_dc(HDC dc, UINT escape_id, hwc_ext_info_t *info)
{
	HANDLE process = NULL;
	hwc_request_t req;
	hwc_result_t res;
	DWORD base0;

	memset(&req, 0, sizeof(req));
	req.which = HWCEXT_GETDRIVERVERSION;
	/*
	 * Win9x Napalm marks GETDRIVERVERSION obsolete and returns -1 even
	 * though the rest of EXT_HWC works. Treat it as best-effort.
	 */
	if (call_escape(dc, escape_id, &req, &res) && res.res_status == 1) {
		info->driver_major = res.opt.driver_version.major;
		info->driver_minor = res.opt.driver_version.minor;
	}

	memset(&req, 0, sizeof(req));
	req.which = HWCEXT_GETDEVICECONFIG;
	req.opt.device_config.dc = dc;
	req.opt.device_config.dev_no = 0;
	if (!call_escape(dc, escape_id, &req, &res) || res.res_status != 1) {
		wsprintfA(info->status, "3dfx drv: EXT %04X device config failed",
			  escape_id);
		return 1;
	}

	info->vendor_id = res.opt.device_config.vendor_id;
	info->device_id = res.opt.device_config.device_id;
	info->fb_ram = res.opt.device_config.fb_ram;
	info->chip_rev = res.opt.device_config.chip_rev;

	process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, GetCurrentProcessId());

	memset(&req, 0, sizeof(req));
	req.which = HWCEXT_GETLINEARADDR;
	req.opt.linear_addr.dev_num = 0;
	req.opt.linear_addr.process_handle = (DWORD)process;
	if (!call_escape(dc, escape_id, &req, &res) || res.res_status != 1) {
		if (process)
			CloseHandle(process);
		wsprintfA(info->status, "3dfx drv: EXT %04X linear addr failed",
			  escape_id);
		return 1;
	}

	info->num_base_addrs = res.opt.linear_addr.num_base_addrs;
	info->base0 = res.opt.linear_addr.base_addresses[0];
	base0 = info->base0;
	if (base0 && !IsBadReadPtr((const void *)(base0 + MXM_PLLCTRL1), sizeof(DWORD)))
		info->pllctrl1 = *(volatile DWORD *)(base0 + MXM_PLLCTRL1);

	if (process)
		CloseHandle(process);

	info->available = 1;
	info->escape_id = escape_id;
	wsprintfA(info->status,
		  "3dfx drv: EXT %04X dev %04X:%04X PLL %08X",
		  escape_id, info->vendor_id, info->device_id, info->pllctrl1);
	return 0;
}

static int probe_one(HWND hwnd, UINT escape_id, hwc_ext_info_t *info)
{
	HDC dc;

	dc = GetDC(hwnd);
	if (dc) {
		if (!probe_dc(dc, escape_id, info)) {
			ReleaseDC(hwnd, dc);
			return 0;
		}
		ReleaseDC(hwnd, dc);
	}

	dc = GetDC(NULL);
	if (dc) {
		if (!probe_dc(dc, escape_id, info)) {
			ReleaseDC(NULL, dc);
			return 0;
		}
		ReleaseDC(NULL, dc);
	}

	dc = CreateDCA("DISPLAY", NULL, NULL, NULL);
	if (dc) {
		if (!probe_dc(dc, escape_id, info)) {
			DeleteDC(dc);
			return 0;
		}
		DeleteDC(dc);
	}

	return 1;
}

int hwc_ext_probe(HWND hwnd, hwc_ext_info_t *info)
{
	memset(info, 0, sizeof(*info));

	if (!probe_one(hwnd, EXT_HWC_NEW, info))
		return 0;

	if (!info->status[0])
		lstrcpynA(info->status, "3dfx driver interface: not available",
			  sizeof(info->status));
	return 1;
}
