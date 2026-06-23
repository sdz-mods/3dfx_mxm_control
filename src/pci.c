#include "pci.h"

#include "io_backend.h"

#include <string.h>

#define PCI_ADDR_PORT 0x0cf8
#define PCI_DATA_PORT 0x0cfc

#ifndef NTSTATUS
#define NTSTATUS LONG
#endif
#ifndef BUS_DATA_TYPE
#define BUS_DATA_TYPE LONG
#endif
#ifndef PCIConfiguration
#define PCIConfiguration (BUS_DATA_TYPE)4
#endif
#ifndef SYSDBG_COMMAND
#define SYSDBG_COMMAND ULONG
#endif
#ifndef SysDbgReadBusData
#define SysDbgReadBusData (SYSDBG_COMMAND)18
#endif
#ifndef SysDbgWriteBusData
#define SysDbgWriteBusData (SYSDBG_COMMAND)19
#endif
#ifndef STATUS_ACCESS_DENIED
#define STATUS_ACCESS_DENIED (NTSTATUS)0xC0000022
#endif

typedef struct {
	ULONG address;
	PVOID buffer;
	ULONG request;
	BUS_DATA_TYPE bus_data_type;
	ULONG bus_number;
	ULONG slot_number;
} sysdbg_bus_data_t;

typedef NTSTATUS (NTAPI *nt_system_debug_control_fn)(SYSDBG_COMMAND command,
	PVOID input, ULONG input_len, PVOID output, ULONG output_len,
	PULONG return_len);

static nt_system_debug_control_fn nt_system_debug_control;
static int nt_sysdbg_ready = -1;

static int enable_debug_privilege(void)
{
	HANDLE token;
	TOKEN_PRIVILEGES tp;

	if (!OpenProcessToken(GetCurrentProcess(),
			TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
		return -1;

	memset(&tp, 0, sizeof(tp));
	tp.PrivilegeCount = 1;
	if (!LookupPrivilegeValueA(NULL, SE_DEBUG_NAME, &tp.Privileges[0].Luid)) {
		CloseHandle(token);
		return -1;
	}

	tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	SetLastError(ERROR_SUCCESS);
	if (!AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), NULL, NULL)) {
		CloseHandle(token);
		return -1;
	}

	CloseHandle(token);
	return GetLastError() == ERROR_NOT_ALL_ASSIGNED ? -1 : 0;
}

static int nt_sysdbg_setup(void)
{
	HMODULE ntdll;

	if (nt_sysdbg_ready >= 0)
		return nt_sysdbg_ready;

	if (!io_is_nt_family()) {
		nt_sysdbg_ready = 0;
		return 0;
	}

	ntdll = GetModuleHandleA("ntdll.dll");
	if (!ntdll) {
		nt_sysdbg_ready = 0;
		return 0;
	}

	nt_system_debug_control =
		(nt_system_debug_control_fn)GetProcAddress(ntdll,
			"NtSystemDebugControl");
	if (!nt_system_debug_control) {
		nt_sysdbg_ready = 0;
		return 0;
	}

	nt_sysdbg_ready = 1;
	return 1;
}

static NTSTATUS nt_sysdbg_pci(BOOL write, pci_bdf_t bdf, BYTE reg, void *buf,
	ULONG len, PULONG ret_len)
{
	sysdbg_bus_data_t cmd;

	if (!nt_sysdbg_setup())
		return (NTSTATUS)0xC0000002;

	memset(&cmd, 0, sizeof(cmd));
	cmd.address = reg;
	cmd.buffer = buf;
	cmd.request = len;
	cmd.bus_data_type = PCIConfiguration;
	cmd.bus_number = bdf.bus;
	cmd.slot_number = ((ULONG)bdf.dev & 0x1f) | (((ULONG)bdf.func & 0x07) << 5);

	*ret_len = 0;
	return nt_system_debug_control(write ? SysDbgWriteBusData : SysDbgReadBusData,
		&cmd, sizeof(cmd), NULL, 0, ret_len);
}

static int nt_pci_read(pci_bdf_t bdf, BYTE reg, void *buf, ULONG len)
{
	NTSTATUS st;
	ULONG ret_len;

	st = nt_sysdbg_pci(FALSE, bdf, reg, buf, len, &ret_len);
	if (st == STATUS_ACCESS_DENIED && enable_debug_privilege() == 0)
		st = nt_sysdbg_pci(FALSE, bdf, reg, buf, len, &ret_len);

	return st < 0 || ret_len != len ? -1 : 0;
}

static int nt_pci_write(pci_bdf_t bdf, BYTE reg, const void *buf, ULONG len)
{
	NTSTATUS st;
	ULONG ret_len;

	st = nt_sysdbg_pci(TRUE, bdf, reg, (void *)buf, len, &ret_len);
	if (st == STATUS_ACCESS_DENIED && enable_debug_privilege() == 0)
		st = nt_sysdbg_pci(TRUE, bdf, reg, (void *)buf, len, &ret_len);

	return st < 0 || ret_len != len ? -1 : 0;
}

static DWORD pci_addr(pci_bdf_t bdf, BYTE reg)
{
	return 0x80000000UL
		| ((DWORD)bdf.bus << 16)
		| ((DWORD)bdf.dev << 11)
		| ((DWORD)bdf.func << 8)
		| (reg & 0xfc);
}

int pci_read8(pci_bdf_t bdf, BYTE reg, BYTE *value)
{
	if (io_is_nt_family())
		return nt_pci_read(bdf, reg, value, sizeof(*value));
	if (io_write32(PCI_ADDR_PORT, pci_addr(bdf, reg)))
		return -1;
	return io_read8(PCI_DATA_PORT + (reg & 3), value);
}

int pci_read16(pci_bdf_t bdf, BYTE reg, WORD *value)
{
	if (io_is_nt_family())
		return nt_pci_read(bdf, reg, value, sizeof(*value));
	if (io_write32(PCI_ADDR_PORT, pci_addr(bdf, reg)))
		return -1;
	return io_read16(PCI_DATA_PORT + (reg & 2), value);
}

int pci_read32(pci_bdf_t bdf, BYTE reg, DWORD *value)
{
	if (io_is_nt_family())
		return nt_pci_read(bdf, reg, value, sizeof(*value));
	if (io_write32(PCI_ADDR_PORT, pci_addr(bdf, reg)))
		return -1;
	return io_read32(PCI_DATA_PORT, value);
}

int pci_write8(pci_bdf_t bdf, BYTE reg, BYTE value)
{
	if (io_is_nt_family())
		return nt_pci_write(bdf, reg, &value, sizeof(value));
	if (io_write32(PCI_ADDR_PORT, pci_addr(bdf, reg)))
		return -1;
	return io_write8(PCI_DATA_PORT + (reg & 3), value);
}

int pci_write16(pci_bdf_t bdf, BYTE reg, WORD value)
{
	if (io_is_nt_family())
		return nt_pci_write(bdf, reg, &value, sizeof(value));
	if (io_write32(PCI_ADDR_PORT, pci_addr(bdf, reg)))
		return -1;
	return io_write16(PCI_DATA_PORT + (reg & 2), value);
}

int pci_write32(pci_bdf_t bdf, BYTE reg, DWORD value)
{
	if (io_is_nt_family())
		return nt_pci_write(bdf, reg, &value, sizeof(value));
	if (io_write32(PCI_ADDR_PORT, pci_addr(bdf, reg)))
		return -1;
	return io_write32(PCI_DATA_PORT, value);
}

int pci_find_device(WORD vendor, WORD device, pci_bdf_t *out)
{
	pci_bdf_t bdf;

	for (bdf.bus = 0; ; bdf.bus++) {
		for (bdf.dev = 0; bdf.dev < 32; bdf.dev++) {
			for (bdf.func = 0; bdf.func < 8; bdf.func++) {
				WORD v = 0xffff;
				WORD d = 0xffff;

				if (pci_read16(bdf, 0x00, &v) || v == 0xffff)
					continue;
				if (pci_read16(bdf, 0x02, &d))
					continue;
				if (v == vendor && d == device) {
					*out = bdf;
					return 0;
				}
			}
		}
		if (bdf.bus == 0xff)
			break;
	}

	return -1;
}
