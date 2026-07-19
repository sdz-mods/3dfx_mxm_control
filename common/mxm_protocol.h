#ifndef MXM_PROTOCOL_H
#define MXM_PROTOCOL_H

#define MXM_XIO_VENDOR 0x104c
#define MXM_XIO_DEVICE 0x8240
#define MXM_SSVID_3DFX 0x4444

/* XIO host protocol version reported in read byte [0] (see MSP XIO_PROTOCOL.md) */
#define MXM_PROTO_VERSION 2
#define MXM_READ_BYTES 29
/*
 * Read block v2 (live data first so a short read gets the scaler status fast):
 *   [0]  proto_version
 *   [1]  scaler flags (b0 link, b1 lock)
 *   [2]  input width  lo   [3] input width  hi
 *   [4]  input height lo   [5] input height hi   <- short "status" read ends here
 *   [6]  ext temp  [7] int temp  [8] fan
 *   [9]  backlight [10] vcore [11] fbsize [12] blank
 *   [13] dos43 [14] sharpness(legacy) [15] contrast [16] peaking
 *   [17] rgb R [18] rgb G [19] rgb B
 *   [20] active filter family
 *   [21..24] per-family filter p1 (index by family)
 *   [25..28] per-family filter p2 (index by family)
 */
#define MXM_STATUS_BYTES 6

/* v2 write register indices (host write = [index, value]; index < 0x80) */
#define MXM_REG_BACKLIGHT     0x00
#define MXM_REG_VCORE         0x01
#define MXM_REG_FBSIZE        0x02
#define MXM_REG_BLANK_FIX     0x03
#define MXM_REG_DOS43         0x10
#define MXM_REG_SHARPNESS     0x11   /* legacy; superseded by filter family/p1/p2 */
#define MXM_REG_CONTRAST      0x12
#define MXM_REG_PEAKING       0x13
#define MXM_REG_RGB_R         0x14
#define MXM_REG_RGB_G         0x15
#define MXM_REG_RGB_B         0x16
#define MXM_REG_FILTER_FAMILY 0x17
#define MXM_REG_FILTER_P1     0x18
#define MXM_REG_FILTER_P2     0x19

/* scale-up FIR families (must match MSP fir_gen.h) */
#define MXM_FIR_FAM_MITCHELL  0
#define MXM_FIR_FAM_KEYS      1
#define MXM_FIR_FAM_GAUSS     2
#define MXM_FIR_FAM_LANCZOS   3
#define MXM_FIR_FAM_COUNT     4

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
