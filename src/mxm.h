#ifndef MXM_H
#define MXM_H

#include <windows.h>

#include "pci.h"

typedef enum {
	MXM_CARD_NONE = 0,
	MXM_CARD_M4800,
	MXM_CARD_M3800,
} mxm_card_type_t;

typedef struct {
	int present;
	mxm_card_type_t type;
	pci_bdf_t bridge;
	WORD ssid;
	char model[32];
	char gpu[16];
	char display[8];
	char revision[8];
} mxm_card_t;

typedef struct {
	BYTE raw[5];
	int brightness;
	int vcore_deci;
	int fb_mb;
	int blank_fix;
	int gpu_temp;
	int smc_temp;
	int fan_speed;
} mxm_settings_t;

int mxm_detect(mxm_card_t *card);
int mxm_read_settings(const mxm_card_t *card, mxm_settings_t *settings);
int mxm_write_settings(const mxm_card_t *card, const mxm_settings_t *settings);
void mxm_defaults(const mxm_card_t *card, mxm_settings_t *settings);
void mxm_card_clear(mxm_card_t *card);
const char *mxm_last_error(void);

#endif
