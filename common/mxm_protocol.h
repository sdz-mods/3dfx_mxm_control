#ifndef MXM_PROTOCOL_H
#define MXM_PROTOCOL_H

#define MXM_XIO_VENDOR 0x104c
#define MXM_XIO_DEVICE 0x8240
#define MXM_SSVID_3DFX 0x4444

#define MXM_GPIO_DIR  0xb4
#define MXM_GPIO_DATA 0xb6
#define MXM_GPIO_00   0x1c
#define MXM_GPIO_01   0x1d
#define MXM_GPIO_10   0x1e
#define MXM_GPIO_11   0x1f

#define MXM_PLLCTRL1 0x44
#define MXM_CLOCK_MIN_MHZ 150
#define MXM_CLOCK_MAX_MHZ 220
#define MXM_CLOCK_DEFAULT_MHZ 166

typedef enum {
	MXM_CARD_NONE = 0,
	MXM_CARD_M4800,
	MXM_CARD_M3800
} mxm_card_type_t;

#endif
