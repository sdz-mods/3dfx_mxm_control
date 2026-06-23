#ifndef IO_BACKEND_H
#define IO_BACKEND_H

#include <windows.h>

int io_init(char *err, int err_len);
void io_shutdown(void);
const char *io_backend_name(void);
int io_is_nt_family(void);

int io_read8(USHORT port, BYTE *value);
int io_read16(USHORT port, WORD *value);
int io_read32(USHORT port, DWORD *value);
int io_write8(USHORT port, BYTE value);
int io_write16(USHORT port, WORD value);
int io_write32(USHORT port, DWORD value);

#endif
