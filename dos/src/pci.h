#ifndef DOS_PCI_H
#define DOS_PCI_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;

typedef struct {
	u8 bus;
	u8 dev;
	u8 func;
} pci_bdf_t;

u8 pci_read8(pci_bdf_t bdf, u8 reg);
u16 pci_read16(pci_bdf_t bdf, u8 reg);
u32 pci_read32(pci_bdf_t bdf, u8 reg);
void pci_write8(pci_bdf_t bdf, u8 reg, u8 value);
int pci_find_device(u16 vendor, u16 device, pci_bdf_t *out);

#endif
