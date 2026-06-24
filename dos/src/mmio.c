#include <i86.h>

#include "pci.h"

#pragma pack(push, 1)
static struct {
	u32 null_low;
	u32 null_high;
	u16 limit_low;
	u16 base_low;
	u8 base_mid;
	u8 access;
	u8 granularity;
	u8 base_high;
} flat_gdt = {
	0, 0,
	0xffff, 0,
	0, 0x92, 0xcf, 0
};

static struct {
	u16 limit;
	u32 base;
} gdt_pointer;
#pragma pack(pop)

int __cdecl cpu_in_v86(void)
{
	volatile u32 flags = 0;

	__asm {
		pushfd
		pop eax
		mov flags, eax
	}
	return (flags & 0x00020000UL) != 0;
}

static void prepare_gdt(void)
{
	gdt_pointer.limit = sizeof(flat_gdt) - 1;
	gdt_pointer.base = ((u32)FP_SEG(&flat_gdt) << 4) + FP_OFF(&flat_gdt);
}

u32 __cdecl mmio_read32(u32 address)
{
	volatile u32 value = 0;

	prepare_gdt();
	__asm {
		pushf
		cli
		push bx
		push edi
		push fs
		lgdt fword ptr gdt_pointer
		mov eax, cr0
		or al, 1
		mov cr0, eax
		mov bx, 8
		mov fs, bx
		and al, 0feh
		mov cr0, eax
		mov edi, address
		mov eax, dword ptr fs:[edi]
		mov value, eax
		pop fs
		pop edi
		pop bx
		popf
	}
	return value;
}

void __cdecl mmio_write32(u32 address, u32 value)
{
	prepare_gdt();
	__asm {
		pushf
		cli
		push bx
		push edi
		push fs
		lgdt fword ptr gdt_pointer
		mov eax, cr0
		or al, 1
		mov cr0, eax
		mov bx, 8
		mov fs, bx
		and al, 0feh
		mov cr0, eax
		mov edi, address
		mov eax, value
		mov dword ptr fs:[edi], eax
		pop fs
		pop edi
		pop bx
		popf
	}
}
