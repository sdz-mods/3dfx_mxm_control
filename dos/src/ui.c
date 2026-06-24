#include <conio.h>
#include <i86.h>
#include <stdio.h>
#include <string.h>

#include "clock.h"
#include "mxm.h"

#define ATTR_NORMAL 0x17
#define ATTR_TITLE 0x1f
#define ATTR_DIM 0x18
#define ATTR_SELECT 0x70
#define ATTR_STATUS 0x1e

enum {
	ITEM_BRIGHTNESS,
	ITEM_VCORE,
	ITEM_CLOCK,
	ITEM_FB,
	ITEM_BLANK,
	ITEM_COUNT
};

static unsigned short far *video =
	(unsigned short far *)MK_FP(0xb800, 0);
static mxm_card_t card;
static mxm_settings_t settings;
static clock_info_t clock_info;
static int clock_mhz = MXM_CLOCK_DEFAULT_MHZ;
static int selected;
static char status_text[78];

static void cell(int x, int y, int ch, int attr)
{
	video[y * 80 + x] = (unsigned short)(ch | (attr << 8));
}

static void fill(int x, int y, int width, int ch, int attr)
{
	int i;

	for (i = 0; i < width; i++)
		cell(x + i, y, ch, attr);
}

static void text(int x, int y, const char *value, int attr)
{
	while (*value && x < 80)
		cell(x++, y, (unsigned char)*value++, attr);
}

static void field(int x, int y, int width, const char *value, int attr)
{
	fill(x, y, width, ' ', attr);
	text(x, y, value, attr);
}

static void box(int x, int y, int width, int height, const char *title)
{
	int row;

	cell(x, y, 218, ATTR_NORMAL);
	fill(x + 1, y, width - 2, 196, ATTR_NORMAL);
	cell(x + width - 1, y, 191, ATTR_NORMAL);
	cell(x, y + height - 1, 192, ATTR_NORMAL);
	fill(x + 1, y + height - 1, width - 2, 196, ATTR_NORMAL);
	cell(x + width - 1, y + height - 1, 217, ATTR_NORMAL);
	for (row = 1; row < height - 1; row++) {
		cell(x, y + row, 179, ATTR_NORMAL);
		cell(x + width - 1, y + row, 179, ATTR_NORMAL);
	}
	if (title) {
		cell(x + 2, y, ' ', ATTR_NORMAL);
		text(x + 3, y, title, ATTR_TITLE);
		cell(x + 3 + strlen(title), y, ' ', ATTR_NORMAL);
	}
}

static void value_line(int row, const char *label, const char *value,
		       int item, int editable)
{
	int attr = item == selected ? ATTR_SELECT :
		(editable ? ATTR_NORMAL : ATTR_DIM);

	field(5, row, 31, label, attr);
	field(37, row, 36, value, attr);
}

static void draw(void)
{
	char buf[80];
	int fb_editable = card.type == MXM_CARD_M4800;
	int blank_editable = card.type == MXM_CARD_M4800;

	fill(0, 0, 80 * 25, ' ', ATTR_NORMAL);
	field(0, 0, 80, "  3dfx MXM Control 1.0 for DOS", ATTR_TITLE);

	box(2, 2, 76, 6, "Card Information");
	sprintf(buf, "Card Model: %s %s", card.model, card.revision);
	field(5, 3, 35, buf, ATTR_NORMAL);
	sprintf(buf, "GPU Model: %s", card.gpu);
	field(42, 3, 31, buf, ATTR_NORMAL);
	sprintf(buf, "Display: %s", card.display);
	field(5, 4, 35, buf, ATTR_NORMAL);
	sprintf(buf, "Bridge: %02X:%02X.%u", card.bridge.bus,
		card.bridge.dev, card.bridge.func);
	field(42, 4, 31, buf, ATTR_NORMAL);
	sprintf(buf, "GPU: %d C   SMC: %d C   Fan: %d%%",
		settings.gpu_temp, settings.smc_temp, settings.fan_speed);
	field(5, 6, 68, buf, ATTR_NORMAL);

	box(2, 8, 76, 9, "Card Settings");
	sprintf(buf, "[%d%%]", settings.brightness);
	value_line(10, "Panel Backlight", buf, ITEM_BRIGHTNESS, 1);
	sprintf(buf, "[%d.%d V]", settings.vcore_deci / 10,
		settings.vcore_deci % 10);
	value_line(11, "Core Voltage", buf, ITEM_VCORE, 1);
	if (clock_info.available)
		sprintf(buf, "[%d MHz]", clock_mhz);
	else
		strcpy(buf, "[Unavailable]");
	value_line(12, "Core/Memory Clock", buf, ITEM_CLOCK,
		   clock_info.available);
	sprintf(buf, "[%d MB]", settings.fb_mb);
	value_line(14, "Framebuffer Memory", buf, ITEM_FB, fb_editable);
	strcpy(buf, settings.blank_fix ? "[Enabled]" : "[Disabled]");
	value_line(15, card.type == MXM_CARD_M3800 ?
		   "Avenger Blank Fix" : "VSA NT Blank Fix",
		   buf, ITEM_BLANK, blank_editable);

	box(2, 17, 76, 4, "Status");
	field(5, 18, 68, status_text, ATTR_STATUS);
	field(5, 19, 68, clock_info.status, ATTR_DIM);
	field(0, 22, 80,
	      " Up/Down Select  Left/Right Change  F5 Refresh  F9 Defaults",
	      ATTR_TITLE);
	field(0, 23, 80,
	      " F10 Apply  F1 Help  Esc Exit", ATTR_TITLE);
}

static void set_status(const char *message)
{
	strncpy(status_text, message, sizeof(status_text) - 1);
	status_text[sizeof(status_text) - 1] = '\0';
}

static int refresh(void)
{
	if (mxm_detect(&card)) {
		set_status(mxm_last_error());
		return -1;
	}
	if (mxm_read_settings(&card, &settings)) {
		set_status(mxm_last_error());
		return -1;
	}
	clock_probe(&card, &clock_info);
	clock_mhz = clock_get_mhz(&clock_info);
	set_status("Ready.");
	return 0;
}

static int item_editable(int item)
{
	if (item == ITEM_CLOCK)
		return clock_info.available;
	if (item == ITEM_FB || item == ITEM_BLANK)
		return card.type == MXM_CARD_M4800;
	return 1;
}

static void move_selection(int direction)
{
	int next = selected;

	do {
		next = (next + direction + ITEM_COUNT) % ITEM_COUNT;
	} while (!item_editable(next) && next != selected);
	selected = next;
}

static void change_value(int direction)
{
	switch (selected) {
	case ITEM_BRIGHTNESS:
		settings.brightness += direction * 5;
		if (settings.brightness < 10)
			settings.brightness = 10;
		if (settings.brightness > 100)
			settings.brightness = 100;
		break;
	case ITEM_VCORE:
		settings.vcore_deci += direction;
		if (settings.vcore_deci < 25)
			settings.vcore_deci = 25;
		if (settings.vcore_deci > 31)
			settings.vcore_deci = 31;
		break;
	case ITEM_CLOCK:
		clock_mhz += direction;
		if (clock_mhz < MXM_CLOCK_MIN_MHZ)
			clock_mhz = MXM_CLOCK_MIN_MHZ;
		if (clock_mhz > MXM_CLOCK_MAX_MHZ)
			clock_mhz = MXM_CLOCK_MAX_MHZ;
		break;
	case ITEM_FB:
		settings.fb_mb = settings.fb_mb == 32 ? 64 : 32;
		break;
	case ITEM_BLANK:
		settings.blank_fix = !settings.blank_fix;
		break;
	}
}

static void apply(void)
{
	set_status("Applying settings...");
	draw();
	if (mxm_write_settings(&card, &settings)) {
		set_status(mxm_last_error());
		return;
	}
	if (clock_info.available && clock_set_mhz(&clock_info, clock_mhz)) {
		set_status("Card settings applied, but clock programming failed.");
		return;
	}
	set_status("Settings applied. Framebuffer size changes after reboot.");
}

static void help(void)
{
	box(8, 5, 64, 13, "Help");
	field(11, 7, 58, "Backlight and voltage apply immediately.", ATTR_NORMAL);
	field(11, 8, 58, "Clock applies immediately and is not stored.", ATTR_NORMAL);
	field(11, 9, 58, "Framebuffer size is stored and requires reboot.", ATTR_NORMAL);
	field(11, 10, 58, "Avoid 64 MB framebuffer mode under Windows 98.", ATTR_NORMAL);
	field(11, 11, 58, "Blank Fix is stored and needs a mode change.", ATTR_NORMAL);
	field(11, 13, 58, "Clock access requires pure real mode without EMM386.", ATTR_DIM);
	field(11, 15, 58, "Press any key to return.", ATTR_TITLE);
	getch();
}

int main(void)
{
	int done = 0;
	union REGS regs;

	regs.h.ah = 0x01;
	regs.h.ch = 0x20;
	regs.h.cl = 0x00;
	int86(0x10, &regs, &regs);
	set_status("Detecting card...");
	refresh();

	while (!done) {
		int key;

		draw();
		key = getch();
		if (key == 0 || key == 0xe0) {
			key = getch();
			switch (key) {
			case 72:
				move_selection(-1);
				break;
			case 80:
				move_selection(1);
				break;
			case 75:
				change_value(-1);
				break;
			case 77:
				change_value(1);
				break;
			case 63:
				set_status("Refreshing...");
				draw();
				refresh();
				break;
			case 67:
				mxm_defaults(&card, &settings);
				clock_mhz = MXM_CLOCK_DEFAULT_MHZ;
				set_status("Defaults loaded. Press F10 to apply.");
				break;
			case 68:
				apply();
				break;
			case 59:
				help();
				break;
			}
		} else if (key == 27) {
			done = 1;
		}
	}

	regs.h.ah = 0x01;
	regs.h.ch = 0x06;
	regs.h.cl = 0x07;
	int86(0x10, &regs, &regs);
	fill(0, 0, 80 * 25, ' ', 0x07);
	return 0;
}
