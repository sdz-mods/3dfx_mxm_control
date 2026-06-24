#ifndef DOS_MXM_H
#define DOS_MXM_H

#include "mxm_protocol.h"
#include "pci.h"

typedef struct {
	int present;
	mxm_card_type_t type;
	pci_bdf_t bridge;
	u16 ssid;
	char model[24];
	char gpu[16];
	char display[8];
	char revision[8];
} mxm_card_t;

typedef struct {
	u8 raw[5];
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
int mxm_write_settings(const mxm_card_t *card,
		       const mxm_settings_t *settings);
void mxm_defaults(const mxm_card_t *card, mxm_settings_t *settings);
const char *mxm_last_error(void);

#endif
