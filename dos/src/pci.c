#include <conio.h>

#include "pci.h"

#define PCI_ADDR_PORT 0x0cf8
#define PCI_DATA_PORT 0x0cfc

static u32 io_in32(u16 port);
#pragma aux io_in32 = \
	"in eax, dx" \
	"mov edx, eax" \
	"shr edx, 16" \
	parm [dx] value [dx ax] modify [ax]

static void io_out32(u16 port, u32 value);
#pragma aux io_out32 = \
	"movzx eax, bx" \
	"movzx ecx, cx" \
	"shl ecx, 16" \
	"or eax, ecx" \
	"out dx, eax" \
	parm [dx] [cx bx] modify [ax cx]

static u32 pci_addr(pci_bdf_t bdf, u8 reg)
{
	return 0x80000000UL |
		((u32)bdf.bus << 16) |
		((u32)bdf.dev << 11) |
		((u32)bdf.func << 8) |
		(reg & 0xfc);
}

u8 pci_read8(pci_bdf_t bdf, u8 reg)
{
	io_out32(PCI_ADDR_PORT, pci_addr(bdf, reg));
	return (u8)inp(PCI_DATA_PORT + (reg & 3));
}

u16 pci_read16(pci_bdf_t bdf, u8 reg)
{
	io_out32(PCI_ADDR_PORT, pci_addr(bdf, reg));
	return (u16)inpw(PCI_DATA_PORT + (reg & 2));
}

u32 pci_read32(pci_bdf_t bdf, u8 reg)
{
	io_out32(PCI_ADDR_PORT, pci_addr(bdf, reg));
	return io_in32(PCI_DATA_PORT);
}

void pci_write8(pci_bdf_t bdf, u8 reg, u8 value)
{
	io_out32(PCI_ADDR_PORT, pci_addr(bdf, reg));
	outp(PCI_DATA_PORT + (reg & 3), value);
}

int pci_find_device(u16 vendor, u16 device, pci_bdf_t *out)
{
	unsigned bus;
	unsigned dev;
	unsigned func;

	for (bus = 0; bus < 256; bus++) {
		for (dev = 0; dev < 32; dev++) {
			for (func = 0; func < 8; func++) {
				pci_bdf_t bdf;
				u16 found_vendor;

				bdf.bus = (u8)bus;
				bdf.dev = (u8)dev;
				bdf.func = (u8)func;
				found_vendor = pci_read16(bdf, 0x00);
				if (found_vendor == 0xffff)
					continue;
				if (found_vendor == vendor &&
				    pci_read16(bdf, 0x02) == device) {
					*out = bdf;
					return 0;
				}
			}
		}
	}

	return -1;
}
