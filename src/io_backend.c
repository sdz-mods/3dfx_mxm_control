#include "io_backend.h"

#include <stdio.h>

static const char *backend = "none";

int io_is_nt_family(void)
{
	OSVERSIONINFOA vi;

	memset(&vi, 0, sizeof(vi));
	vi.dwOSVersionInfoSize = sizeof(vi);
	if (!GetVersionExA(&vi))
		return 1;
	return vi.dwPlatformId == VER_PLATFORM_WIN32_NT;
}

static void set_err(char *err, int err_len, const char *msg)
{
	if (err && err_len > 0) {
		lstrcpynA(err, msg, err_len);
		err[err_len - 1] = '\0';
	}
}

int io_init(char *err, int err_len)
{
	if (!io_is_nt_family()) {
		backend = "direct Win9x I/O";
		return 0;
	}

	backend = "NT SysDbg PCI config";
	set_err(err, err_len, "");
	return 0;
}

void io_shutdown(void)
{
}

const char *io_backend_name(void)
{
	return backend;
}

static BYTE raw_in8(USHORT port)
{
	BYTE value;
	__asm__ __volatile__("inb %w1, %0" : "=a"(value) : "Nd"(port));
	return value;
}

static WORD raw_in16(USHORT port)
{
	WORD value;
	__asm__ __volatile__("inw %w1, %0" : "=a"(value) : "Nd"(port));
	return value;
}

static DWORD raw_in32(USHORT port)
{
	DWORD value;
	__asm__ __volatile__("inl %w1, %0" : "=a"(value) : "Nd"(port));
	return value;
}

static void raw_out8(USHORT port, BYTE value)
{
	__asm__ __volatile__("outb %0, %w1" : : "a"(value), "Nd"(port));
}

static void raw_out16(USHORT port, WORD value)
{
	__asm__ __volatile__("outw %0, %w1" : : "a"(value), "Nd"(port));
}

static void raw_out32(USHORT port, DWORD value)
{
	__asm__ __volatile__("outl %0, %w1" : : "a"(value), "Nd"(port));
}

int io_read8(USHORT port, BYTE *value)
{
	if (io_is_nt_family())
		return -1;
	*value = raw_in8(port);
	return 0;
}

int io_read16(USHORT port, WORD *value)
{
	if (io_is_nt_family())
		return -1;
	*value = raw_in16(port);
	return 0;
}

int io_read32(USHORT port, DWORD *value)
{
	if (io_is_nt_family())
		return -1;
	*value = raw_in32(port);
	return 0;
}

int io_write8(USHORT port, BYTE value)
{
	if (io_is_nt_family())
		return -1;
	raw_out8(port, value);
	return 0;
}

int io_write16(USHORT port, WORD value)
{
	if (io_is_nt_family())
		return -1;
	raw_out16(port, value);
	return 0;
}

int io_write32(USHORT port, DWORD value)
{
	if (io_is_nt_family())
		return -1;
	raw_out32(port, value);
	return 0;
}
