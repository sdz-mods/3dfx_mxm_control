#ifndef PCI_H
#define PCI_H

#include <windows.h>

typedef struct {
	BYTE bus;
	BYTE dev;
	BYTE func;
} pci_bdf_t;

int pci_read8(pci_bdf_t bdf, BYTE reg, BYTE *value);
int pci_read16(pci_bdf_t bdf, BYTE reg, WORD *value);
int pci_read32(pci_bdf_t bdf, BYTE reg, DWORD *value);
int pci_write8(pci_bdf_t bdf, BYTE reg, BYTE value);
int pci_write16(pci_bdf_t bdf, BYTE reg, WORD value);
int pci_write32(pci_bdf_t bdf, BYTE reg, DWORD value);
int pci_find_device(WORD vendor, WORD device, pci_bdf_t *out);

#endif
