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
	u8 raw[MXM_READ_BYTES];
	int proto_version;   /* read byte [0]; MXM_PROTO_VERSION => scaler valid */
	int scaler_capable;  /* 1 if this firmware exposes scaler settings */
	/* power / card */
	int brightness;
	int vcore_deci;
	int fb_mb;
	int blank_fix;
	/* live telemetry */
	int gpu_temp;
	int smc_temp;
	int fan_speed;
	/* scaler image settings (v2 only) */
	int dos43;
	int sharpness;
	int contrast;
	int peaking;
	int rgb_r;
	int rgb_g;
	int rgb_b;
	/* scaler live status (v2 only) */
	int scaler_link;
	int scaler_lock;
	int in_width;
	int in_height;
} mxm_settings_t;

int mxm_detect(mxm_card_t *card);
int mxm_read_settings(const mxm_card_t *card, mxm_settings_t *settings);
/* prev = last-known card state; only registers that differ are written
 * (pass NULL to force a full write). */
int mxm_write_settings(const mxm_card_t *card,
		       const mxm_settings_t *settings,
		       const mxm_settings_t *prev);
void mxm_defaults(const mxm_card_t *card, mxm_settings_t *settings);
const char *mxm_last_error(void);

#endif
