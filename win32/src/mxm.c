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

	for (bit = 0; bit < 40; bit++) {
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

	settings->brightness = settings->raw[0];
	if (settings->brightness < 10)
		settings->brightness = 10;
	if (settings->brightness > 100)
		settings->brightness = 100;

	settings->vcore_deci = ((settings->raw[1] >> 4) & 0x07) + 25;
	settings->fb_mb = card->type == MXM_CARD_M4800
		? (((settings->raw[1] >> 3) & 1) ? 64 : 32) : 16;
	settings->blank_fix = card->type == MXM_CARD_M4800
		? ((settings->raw[1] >> 2) & 1) : 1;
	settings->gpu_temp = settings->raw[2];
	settings->smc_temp = settings->raw[3];
	settings->fan_speed = settings->raw[4];

	gpio_write(card, MXM_GPIO_10);
	return 0;

io_error:
	set_error("GPIO protocol I/O failed while reading card settings.");
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

int mxm_write_settings(const mxm_card_t *card, const mxm_settings_t *settings)
{
	BYTE packet0;
	BYTE packet1;
	int vcore_code;

	if (!card->present) {
		set_error("No supported card detected.");
		return -1;
	}

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

	if (send_byte(card, packet0) || send_byte(card, packet1)) {
		set_error("GPIO protocol I/O failed while writing card settings.");
		return -1;
	}

	return 0;
}

void mxm_defaults(const mxm_card_t *card, mxm_settings_t *settings)
{
	memset(settings, 0, sizeof(*settings));
	settings->brightness = 100;
	settings->vcore_deci = card->type == MXM_CARD_M4800 ? 26 : 25;
	settings->fb_mb = card->type == MXM_CARD_M4800 ? 32 : 16;
	settings->blank_fix = card->type == MXM_CARD_M4800 ? 0 : 1;
}
