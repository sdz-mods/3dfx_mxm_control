#include "mxm.h"

#include <stdio.h>
#include <string.h>

#define NT_GPIO_STEP_MS 1
#define WIN9X_GPIO_STEP_MS 3
#define WIN9X_SAMPLE_SETTLE_MS 4
#define WIN9X_PACKET_GAP_MS 10

static char last_error[128];

static void set_error(const char *msg)
{
	lstrcpynA(last_error, msg, sizeof(last_error));
}

const char *mxm_last_error(void)
{
	return last_error[0] ? last_error : "No error";
}

void mxm_card_clear(mxm_card_t *card)
{
	memset(card, 0, sizeof(*card));
	lstrcpynA(card->model, "No supported card", sizeof(card->model));
	lstrcpynA(card->gpu, "Unknown", sizeof(card->gpu));
	lstrcpynA(card->display, "Unknown", sizeof(card->display));
	lstrcpynA(card->revision, "Unknown", sizeof(card->revision));
}

static void decode_card(mxm_card_t *card)
{
	int family = (card->ssid >> 8) & 0xff;
	int rev = card->ssid & 0xff;

	card->present = 0;
	card->type = MXM_CARD_NONE;

	if (family == 0x01 || family == 0x02) {
		card->present = 1;
		card->type = MXM_CARD_M4800;
		lstrcpynA(card->model, family == 0x01 ? "M4800 LVDS" : "M4800 eDP",
			  sizeof(card->model));
		lstrcpynA(card->gpu, "Napalm x1", sizeof(card->gpu));
	} else if (family == 0x03 || family == 0x04) {
		card->present = 1;
		card->type = MXM_CARD_M3800;
		lstrcpynA(card->model, family == 0x03 ? "M3800 LVDS" : "M3800 eDP",
			  sizeof(card->model));
		lstrcpynA(card->gpu, "Avenger", sizeof(card->gpu));
	} else {
		return;
	}

	lstrcpynA(card->display, (family & 1) ? "LVDS" : "eDP", sizeof(card->display));
	wsprintfA(card->revision, "A%02d", rev);
}

int mxm_detect(mxm_card_t *card)
{
	WORD ssvid = 0xffff;

	last_error[0] = '\0';
	mxm_card_clear(card);

	if (pci_find_device(MXM_XIO_VENDOR, MXM_XIO_DEVICE, &card->bridge)) {
		set_error("TI XIO2001 bridge was not found.");
		return -1;
	}

	if (pci_write8(card->bridge, MXM_GPIO_DIR, 0x03)) {
		set_error("Failed to configure XIO2001 GPIO direction.");
		return -1;
	}

	if (pci_read16(card->bridge, 0x44, &ssvid)) {
		set_error("Failed to read MXM subsystem vendor ID.");
		return -1;
	}
	if (ssvid != MXM_SSVID_3DFX) {
		set_error("XIO2001 bridge is not a 3dfx MXM card.");
		return -1;
	}

	if (pci_read16(card->bridge, 0x46, &card->ssid)) {
		set_error("Failed to read MXM subsystem ID.");
		return -1;
	}

	decode_card(card);
	if (!card->present) {
		set_error("Unsupported 3dfx MXM subsystem ID.");
		return -1;
	}

	return 0;
}

static int gpio_write(const mxm_card_t *card, BYTE value)
{
	return pci_write8(card->bridge, MXM_GPIO_DATA, value);
}

static int gpio_read(const mxm_card_t *card, BYTE *value)
{
	return pci_read8(card->bridge, MXM_GPIO_DATA, value);
}

static int is_nt_family(void)
{
	static int cached = -1;
	OSVERSIONINFOA vi;

	if (cached >= 0)
		return cached;

	memset(&vi, 0, sizeof(vi));
	vi.dwOSVersionInfoSize = sizeof(vi);
	if (!GetVersionExA(&vi))
		cached = 1;
	else
		cached = vi.dwPlatformId == VER_PLATFORM_WIN32_NT;
	return cached;
}

static void gpio_step_delay(void)
{
	Sleep(is_nt_family() ? NT_GPIO_STEP_MS : WIN9X_GPIO_STEP_MS);
}

static void gpio_sample_settle(void)
{
	if (!is_nt_family())
		Sleep(WIN9X_SAMPLE_SETTLE_MS);
}

static void packet_gap_delay(void)
{
	if (!is_nt_family())
		Sleep(WIN9X_PACKET_GAP_MS);
}

static void decode_legacy(const mxm_card_t *card, mxm_settings_t *s)
{
	/* old 5-byte firmware: [backlight, packed vcore/fb/blank, gpuT, smcT, fan] */
	s->brightness = s->raw[0];
	s->vcore_deci = ((s->raw[1] >> 4) & 0x07) + 25;
	s->fb_mb = card->type == MXM_CARD_M4800
		? (((s->raw[1] >> 3) & 1) ? 64 : 32) : 16;
	s->blank_fix = card->type == MXM_CARD_M4800
		? ((s->raw[1] >> 2) & 1) : 1;
	s->gpu_temp = s->raw[2];
	s->smc_temp = s->raw[3];
	s->fan_speed = s->raw[4];
	s->scaler_capable = 0;
}

/* scaler live status = leading bytes [1..5] (read by a short "status" read) */
static void decode_status(mxm_settings_t *s)
{
	s->scaler_link = (s->raw[1] & 0x01) ? 1 : 0;
	s->scaler_lock = (s->raw[1] & 0x02) ? 1 : 0;
	s->in_width = s->raw[2] | ((int)s->raw[3] << 8);
	s->in_height = s->raw[4] | ((int)s->raw[5] << 8);
	s->scaler_capable = 1;
}

static void decode_v2(mxm_settings_t *s)
{
	/* v2 20-byte block; live data first, then settings. See mxm_protocol.h */
	decode_status(s);
	s->gpu_temp = s->raw[6];
	s->smc_temp = s->raw[7];
	s->fan_speed = s->raw[8];
	s->brightness = s->raw[9];
	s->vcore_deci = s->raw[10] + 25;
	s->fb_mb = s->raw[11] ? 64 : 32;
	s->blank_fix = s->raw[12];
	s->dos43 = s->raw[13];
	s->sharpness = s->raw[14];
	s->contrast = s->raw[15];
	s->peaking = s->raw[16];
	s->rgb_r = s->raw[17];
	s->rgb_g = s->raw[18];
	s->rgb_b = s->raw[19];
}

int mxm_read_settings(const mxm_card_t *card, mxm_settings_t *settings)
{
	int bit;
	int byte_index = 0;

	memset(settings, 0, sizeof(*settings));
	if (!card->present) {
		set_error("No supported card detected.");
		return -1;
	}

	if (gpio_write(card, MXM_GPIO_10))
		goto io_error;
	Sleep(50);
	if (gpio_write(card, MXM_GPIO_11))
		goto io_error;
	Sleep(200);

	for (bit = 0; bit < MXM_READ_BYTES * 8; bit++) {
		BYTE data = 0;

		if (gpio_write(card, MXM_GPIO_10))
			goto io_error;
		gpio_step_delay();
		if (gpio_write(card, MXM_GPIO_11))
			goto io_error;
		gpio_step_delay();
		gpio_sample_settle();
		if (gpio_read(card, &data))
			goto io_error;

		settings->raw[byte_index] <<= 1;
		settings->raw[byte_index] |= (data & 0x04) >> 2;
		if (bit > 0 && ((bit + 1) % 8) == 0)
			byte_index++;
	}
	gpio_write(card, MXM_GPIO_10);

	settings->proto_version = settings->raw[0];
	if (settings->proto_version == MXM_PROTO_VERSION)
		decode_v2(settings);
	else
		decode_legacy(card, settings);

	if (settings->brightness < 10)
		settings->brightness = 10;
	if (settings->brightness > 100)
		settings->brightness = 100;

	return 0;

io_error:
	set_error("GPIO protocol I/O failed while reading card settings.");
	return -1;
}

/*
 * Short read: clock out only the leading status bytes (link/lock/resolution)
 * and update just those fields, leaving the settings/telemetry from the last
 * full read intact. Much faster than a full read for a status-only refresh.
 * NOTE: this stops the transfer early, so the MSP's read state is left
 * mid-block until its idle timeout (~2 s); the caller must not start another
 * transaction until then (see the app's msp guard).
 */
int mxm_read_status(const mxm_card_t *card, mxm_settings_t *settings)
{
	int bit;
	int byte_index = 0;

	if (!card->present) {
		set_error("No supported card detected.");
		return -1;
	}

	if (gpio_write(card, MXM_GPIO_10))
		goto io_error;
	Sleep(50);
	if (gpio_write(card, MXM_GPIO_11))
		goto io_error;
	Sleep(200);

	for (bit = 0; bit < MXM_STATUS_BYTES * 8; bit++) {
		BYTE data = 0;

		if (gpio_write(card, MXM_GPIO_10))
			goto io_error;
		gpio_step_delay();
		if (gpio_write(card, MXM_GPIO_11))
			goto io_error;
		gpio_step_delay();
		gpio_sample_settle();
		if (gpio_read(card, &data))
			goto io_error;

		settings->raw[byte_index] <<= 1;
		settings->raw[byte_index] |= (data & 0x04) >> 2;
		if (bit > 0 && ((bit + 1) % 8) == 0)
			byte_index++;
	}
	gpio_write(card, MXM_GPIO_10);

	if (settings->raw[0] == MXM_PROTO_VERSION)
		decode_status(settings);
	return 0;

io_error:
	set_error("GPIO protocol I/O failed while reading scaler status.");
	return -1;
}

static int send_byte(const mxm_card_t *card, BYTE packet)
{
	int bit;

	if (gpio_write(card, MXM_GPIO_00))
		return -1;
	gpio_step_delay();

	for (bit = 7; bit >= 0; bit--) {
		if ((packet >> bit) & 1) {
			if (gpio_write(card, MXM_GPIO_11))
				return -1;
		} else {
			if (gpio_write(card, MXM_GPIO_01))
				return -1;
		}
		gpio_step_delay();
		if (gpio_write(card, MXM_GPIO_00))
			return -1;
		gpio_step_delay();
	}

	packet_gap_delay();
	return 0;
}

static int write_register(const mxm_card_t *card, BYTE index, BYTE value)
{
	/* v2 write transaction is [index, value]; index MSB (0) selects write */
	return send_byte(card, index) || send_byte(card, value);
}

static int write_legacy(const mxm_card_t *card, const mxm_settings_t *settings)
{
	BYTE packet0;
	BYTE packet1;
	int vcore_code;

	packet0 = (BYTE)settings->brightness;
	vcore_code = settings->vcore_deci - 25;
	if (vcore_code < 0)
		vcore_code = 0;
	if (vcore_code > 6)
		vcore_code = 6;

	packet1 = (BYTE)(vcore_code << 4);
	if (card->type == MXM_CARD_M4800) {
		if (settings->fb_mb == 64)
			packet1 |= 0x08;
		if (settings->blank_fix)
			packet1 |= 0x04;
	}

	return send_byte(card, packet0) || send_byte(card, packet1);
}

/* write reg only if prev is NULL (force) or the field changed */
#define WR_IF(field, idx, val) \
	do { \
		if (!prev || prev->field != settings->field) \
			err |= write_register(card, (idx), (BYTE)(val)); \
	} while (0)

int mxm_write_settings(const mxm_card_t *card, const mxm_settings_t *settings,
		       const mxm_settings_t *prev)
{
	int vcore_code;
	int err = 0;

	if (!card->present) {
		set_error("No supported card detected.");
		return -1;
	}

	if (!settings->scaler_capable) {
		/* legacy firmware: single combined packet, always written */
		if (write_legacy(card, settings))
			goto io_error;
		return 0;
	}

	vcore_code = settings->vcore_deci - 25;
	if (vcore_code < 0)
		vcore_code = 0;
	if (vcore_code > 6)
		vcore_code = 6;

	WR_IF(brightness, MXM_REG_BACKLIGHT, settings->brightness);
	WR_IF(vcore_deci, MXM_REG_VCORE, vcore_code);
	WR_IF(fb_mb, MXM_REG_FBSIZE, settings->fb_mb == 64 ? 1 : 0);
	WR_IF(blank_fix, MXM_REG_BLANK_FIX, settings->blank_fix ? 1 : 0);
	WR_IF(dos43, MXM_REG_DOS43, settings->dos43 ? 1 : 0);
	WR_IF(sharpness, MXM_REG_SHARPNESS, settings->sharpness);
	WR_IF(contrast, MXM_REG_CONTRAST, settings->contrast);
	WR_IF(peaking, MXM_REG_PEAKING, settings->peaking);
	WR_IF(rgb_r, MXM_REG_RGB_R, settings->rgb_r);
	WR_IF(rgb_g, MXM_REG_RGB_G, settings->rgb_g);
	WR_IF(rgb_b, MXM_REG_RGB_B, settings->rgb_b);

	if (err)
		goto io_error;
	return 0;

io_error:
	set_error("GPIO protocol I/O failed while writing card settings.");
	return -1;
}

void mxm_defaults(const mxm_card_t *card, mxm_settings_t *settings)
{
	memset(settings, 0, sizeof(*settings));
	settings->brightness = 100;
	settings->vcore_deci = card->type == MXM_CARD_M4800 ? 26 : 25;
	settings->fb_mb = card->type == MXM_CARD_M4800 ? 32 : 16;
	settings->blank_fix = card->type == MXM_CARD_M4800 ? 0 : 1;
	/* scaler defaults (match the RTD firmware) */
	settings->dos43 = 1;
	settings->sharpness = 2;
	settings->contrast = 40;
	settings->peaking = 0;
	settings->rgb_r = 50;
	settings->rgb_g = 50;
	settings->rgb_b = 50;
}
