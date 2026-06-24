#include <conio.h>
#include <stdio.h>
#include <string.h>

#include "mxm.h"

#define DOS_GPIO_STEP_MS 3
#define DOS_SAMPLE_SETTLE_MS 4
#define DOS_PACKET_GAP_MS 10

static char last_error[96];

static u16 pit_count(void)
{
	u8 low;
	u8 high;

	outp(0x43, 0x00);
	low = (u8)inp(0x40);
	high = (u8)inp(0x40);
	return ((u16)high << 8) | low;
}

static void delay_ms(unsigned milliseconds)
{
	u32 elapsed = 0;
	u32 target = (u32)milliseconds * 1193UL;
	u16 previous = pit_count();

	while (elapsed < target) {
		u16 current = pit_count();

		elapsed += (u16)(previous - current);
		previous = current;
	}
}

static void set_error(const char *message)
{
	strncpy(last_error, message, sizeof(last_error) - 1);
	last_error[sizeof(last_error) - 1] = '\0';
}

const char *mxm_last_error(void)
{
	return last_error[0] ? last_error : "No error";
}

static void decode_card(mxm_card_t *card)
{
	int family = (card->ssid >> 8) & 0xff;
	int revision = card->ssid & 0xff;

	if (family == 0x01 || family == 0x02) {
		card->present = 1;
		card->type = MXM_CARD_M4800;
		strcpy(card->model, family == 0x01 ? "M4800 LVDS" : "M4800 eDP");
		strcpy(card->gpu, "Napalm x1");
	} else if (family == 0x03 || family == 0x04) {
		card->present = 1;
		card->type = MXM_CARD_M3800;
		strcpy(card->model, family == 0x03 ? "M3800 LVDS" : "M3800 eDP");
		strcpy(card->gpu, "Avenger");
	} else {
		return;
	}

	strcpy(card->display, (family & 1) ? "LVDS" : "eDP");
	sprintf(card->revision, "A%02d", revision);
}

int mxm_detect(mxm_card_t *card)
{
	u16 ssvid;

	memset(card, 0, sizeof(*card));
	last_error[0] = '\0';
	if (pci_find_device(MXM_XIO_VENDOR, MXM_XIO_DEVICE, &card->bridge)) {
		set_error("TI XIO2001 bridge was not found.");
		return -1;
	}

	pci_write8(card->bridge, MXM_GPIO_DIR, 0x03);
	ssvid = pci_read16(card->bridge, 0x44);
	if (ssvid != MXM_SSVID_3DFX) {
		set_error("XIO2001 bridge is not a 3dfx MXM card.");
		return -1;
	}

	card->ssid = pci_read16(card->bridge, 0x46);
	decode_card(card);
	if (!card->present) {
		set_error("Unsupported 3dfx MXM subsystem ID.");
		return -1;
	}
	return 0;
}

static void gpio_write(const mxm_card_t *card, u8 value)
{
	pci_write8(card->bridge, MXM_GPIO_DATA, value);
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

	gpio_write(card, MXM_GPIO_10);
	delay_ms(50);
	gpio_write(card, MXM_GPIO_11);
	delay_ms(200);

	for (bit = 0; bit < 40; bit++) {
		u8 data;

		gpio_write(card, MXM_GPIO_10);
		delay_ms(DOS_GPIO_STEP_MS);
		gpio_write(card, MXM_GPIO_11);
		delay_ms(DOS_GPIO_STEP_MS + DOS_SAMPLE_SETTLE_MS);
		data = pci_read8(card->bridge, MXM_GPIO_DATA);
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
}

static void send_byte(const mxm_card_t *card, u8 packet)
{
	int bit;

	gpio_write(card, MXM_GPIO_00);
	delay_ms(DOS_GPIO_STEP_MS);
	for (bit = 7; bit >= 0; bit--) {
		gpio_write(card, ((packet >> bit) & 1) ?
			   MXM_GPIO_11 : MXM_GPIO_01);
		delay_ms(DOS_GPIO_STEP_MS);
		gpio_write(card, MXM_GPIO_00);
		delay_ms(DOS_GPIO_STEP_MS);
	}
	delay_ms(DOS_PACKET_GAP_MS);
}

int mxm_write_settings(const mxm_card_t *card,
		       const mxm_settings_t *settings)
{
	u8 packet0;
	u8 packet1;
	int vcore_code;

	if (!card->present) {
		set_error("No supported card detected.");
		return -1;
	}

	packet0 = (u8)settings->brightness;
	vcore_code = settings->vcore_deci - 25;
	if (vcore_code < 0)
		vcore_code = 0;
	if (vcore_code > 6)
		vcore_code = 6;
	packet1 = (u8)(vcore_code << 4);
	if (card->type == MXM_CARD_M4800) {
		if (settings->fb_mb == 64)
			packet1 |= 0x08;
		if (settings->blank_fix)
			packet1 |= 0x04;
	}

	send_byte(card, packet0);
	send_byte(card, packet1);
	return 0;
}

void mxm_defaults(const mxm_card_t *card, mxm_settings_t *settings)
{
	int gpu_temp = settings->gpu_temp;
	int smc_temp = settings->smc_temp;
	int fan_speed = settings->fan_speed;

	memset(settings, 0, sizeof(*settings));
	settings->brightness = 100;
	settings->vcore_deci = card->type == MXM_CARD_M4800 ? 26 : 25;
	settings->fb_mb = card->type == MXM_CARD_M4800 ? 32 : 16;
	settings->blank_fix = card->type == MXM_CARD_M4800 ? 0 : 1;
	settings->gpu_temp = gpu_temp;
	settings->smc_temp = smc_temp;
	settings->fan_speed = fan_speed;
}
