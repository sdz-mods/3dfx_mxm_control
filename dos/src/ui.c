#include <conio.h>
#include <i86.h>
#include <stdio.h>
#include <string.h>

#include "clock.h"
#include "mxm.h"
#include "fir_ui.h"

#define ATTR_NORMAL 0x17
#define ATTR_TITLE 0x1f
#define ATTR_DIM 0x18
#define ATTR_SELECT 0x70
#define ATTR_STATUS 0x1e
#define ATTR_TAB_ON 0x70
#define ATTR_TAB_OFF 0x17

enum {
	ITEM_VCORE,
	ITEM_CLOCK,
	ITEM_FB,
	ITEM_BLANK,
	ITEM_BACKLIGHT,
	ITEM_DOS43,
	ITEM_FILTER,
	ITEM_FILTER_P1,
	ITEM_FILTER_P2,
	ITEM_CONTRAST,
	ITEM_PEAKING,
	ITEM_RGB_R,
	ITEM_RGB_G,
	ITEM_RGB_B,
	ITEM_COUNT
};

/* which items live on each page, in navigation order (-1 terminates) */
static const int page_items[2][11] = {
	{ ITEM_VCORE, ITEM_CLOCK, ITEM_FB, ITEM_BLANK,
	  -1, -1, -1, -1, -1, -1, -1 },
	{ ITEM_BACKLIGHT, ITEM_DOS43, ITEM_FILTER, ITEM_FILTER_P1, ITEM_FILTER_P2,
	  ITEM_CONTRAST, ITEM_PEAKING, ITEM_RGB_R, ITEM_RGB_G, ITEM_RGB_B, -1 }
};
static const int page_count[2] = { 4, 10 };

static unsigned short far *video =
	(unsigned short far *)MK_FP(0xb800, 0);
static mxm_card_t card;
static mxm_settings_t settings;
static mxm_settings_t baseline;
static clock_info_t clock_info;
static int clock_mhz = MXM_CLOCK_DEFAULT_MHZ;
static int page;
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

static int item_editable(int item)
{
	switch (item) {
	case ITEM_CLOCK:
		return clock_info.available;
	case ITEM_FB:
	case ITEM_BLANK:
		return card.type == MXM_CARD_M4800;
	case ITEM_DOS43:
	case ITEM_FILTER:
	case ITEM_CONTRAST:
	case ITEM_PEAKING:
	case ITEM_RGB_R:
	case ITEM_RGB_G:
	case ITEM_RGB_B:
		return card.type == MXM_CARD_M4800 && settings.scaler_capable;
	case ITEM_FILTER_P1:
		return card.type == MXM_CARD_M4800 && settings.scaler_capable
		       && fir_ui_p1_max(settings.filter_family) > 0;
	case ITEM_FILTER_P2:
		return card.type == MXM_CARD_M4800 && settings.scaler_capable
		       && fir_ui_p2_max(settings.filter_family) > 0;
	default:
		return card.present;
	}
}

static void draw_item(int row, const char *label, const char *value, int item)
{
	int sel = page_items[page][selected] == item;
	int editable = item_editable(item);
	int attr = sel ? ATTR_SELECT : (editable ? ATTR_NORMAL : ATTR_DIM);

	field(5, row, 31, label, attr);
	field(37, row, 32, value, attr);
}

static void draw_tabs(void)
{
	/* one blank line (row 1) between the title and the tab bar */
	field(0, 2, 80, "", ATTR_NORMAL);
	text(2, 2, " Card Info & Settings ",
	     page == 0 ? ATTR_TAB_ON : ATTR_TAB_OFF);
	text(28, 2, " Image & Panel ",
	     page == 1 ? ATTR_TAB_ON : ATTR_TAB_OFF);
}

static void draw_page0(void)
{
	char buf[80];

	box(2, 3, 76, 5, "Card Information");
	sprintf(buf, "Card Model: %s %s", card.model, card.revision);
	field(5, 4, 35, buf, ATTR_NORMAL);
	sprintf(buf, "GPU: %s", card.gpu);
	field(42, 4, 31, buf, ATTR_NORMAL);
	sprintf(buf, "Display: %s", card.display);
	field(5, 5, 35, buf, ATTR_NORMAL);
	sprintf(buf, "Bridge: %02X:%02X.%u  SSID: %04X", card.bridge.bus,
		card.bridge.dev, card.bridge.func, card.ssid);
	field(42, 5, 31, buf, ATTR_NORMAL);
	sprintf(buf, "GPU: %d C   SMC: %d C   Fan: %d%%",
		settings.gpu_temp, settings.smc_temp, settings.fan_speed);
	field(5, 6, 68, buf, ATTR_NORMAL);

	box(2, 8, 76, 7, "Card Settings");
	sprintf(buf, "[%d.%d V]", settings.vcore_deci / 10,
		settings.vcore_deci % 10);
	draw_item(10, "Core Voltage", buf, ITEM_VCORE);
	if (clock_info.available)
		sprintf(buf, "[%d MHz]", clock_mhz);
	else
		strcpy(buf, "[Unavailable]");
	draw_item(11, "Core/Memory Clock", buf, ITEM_CLOCK);
	sprintf(buf, "[%d MB]", settings.fb_mb);
	draw_item(12, "Framebuffer Memory", buf, ITEM_FB);
	strcpy(buf, settings.blank_fix ? "[Enabled]" : "[Disabled]");
	draw_item(13, "VSA NT Blank Fix", buf, ITEM_BLANK);

	box(2, 15, 76, 3, "Driver Interface");
	field(5, 16, 68, clock_info.status, ATTR_DIM);
}

static void draw_page1(void)
{
	char buf[80];

	box(2, 3, 76, 3, "Panel");
	sprintf(buf, "[%d%%]", settings.brightness);
	draw_item(4, "Panel Backlight", buf, ITEM_BACKLIGHT);

	box(2, 6, 76, 11, "Image / Scaler Settings");
	strcpy(buf, settings.dos43 ? "[On]" : "[Off]");
	draw_item(7, "Force 4:3 for DOS modes", buf, ITEM_DOS43);
	{
		int fam = settings.filter_family;
		char v[8];

		if (fam < 0 || fam >= MXM_FIR_FAM_COUNT)
			fam = MXM_FIR_FAM_MITCHELL;
		sprintf(buf, "[%s]", fir_family_name[fam]);
		draw_item(8, "Scaling Filter", buf, ITEM_FILTER);

		if (fir_ui_p1_max(fam) > 0) {
			fir_ui_fmt_x100(fir_ui_p1_val_x100(fam,
				settings.filter_p1[fam]), v);
			sprintf(buf, "[%s %s]", fir_ui_p1_label(fam), v);
		} else {
			strcpy(buf, "[fixed kernel]");
		}
		draw_item(9, "Filter Parameter 1", buf, ITEM_FILTER_P1);

		if (fir_ui_p2_max(fam) > 0) {
			fir_ui_fmt_x100(fir_ui_p2_val_x100(fam,
				settings.filter_p2[fam]), v);
			sprintf(buf, "[%s %s]", fir_ui_p2_label(fam), v);
		} else {
			strcpy(buf, "[n/a]");
		}
		draw_item(10, "Filter Parameter 2", buf, ITEM_FILTER_P2);
	}
	sprintf(buf, "[%d]", settings.contrast);
	draw_item(11, "Contrast", buf, ITEM_CONTRAST);
	sprintf(buf, "[%d]", settings.peaking);
	draw_item(12, "Edge Enhance", buf, ITEM_PEAKING);
	sprintf(buf, "[%d]", settings.rgb_r);
	draw_item(13, "White Balance R", buf, ITEM_RGB_R);
	sprintf(buf, "[%d]", settings.rgb_g);
	draw_item(14, "White Balance G", buf, ITEM_RGB_G);
	sprintf(buf, "[%d]", settings.rgb_b);
	draw_item(15, "White Balance B", buf, ITEM_RGB_B);

	box(2, 17, 76, 3, "Scaler Status");
	if (!settings.scaler_capable)
		strcpy(buf, "Unavailable");
	else if (!settings.scaler_link)
		strcpy(buf, "Link: no UART link");
	else if (settings.scaler_lock)
		sprintf(buf, "Link: connected, signal locked      %d x %d",
			settings.in_width, settings.in_height);
	else
		strcpy(buf, "Link: connected, no signal");
	field(5, 18, 68, buf,
	      settings.scaler_capable ? ATTR_NORMAL : ATTR_DIM);
}

static void draw(void)
{
	fill(0, 0, 80 * 25, ' ', ATTR_NORMAL);
	field(0, 0, 80, "  3dfx MXM Control 1.2 for DOS", ATTR_TITLE);
	draw_tabs();

	if (page == 0)
		draw_page0();
	else
		draw_page1();

	field(5, 20, 68, status_text, ATTR_STATUS);
	field(0, 23, 80,
	      " Tab Page  Up/Down Select  Left/Right Change  F5 Refresh  F9 Defaults",
	      ATTR_TITLE);
	field(0, 24, 80, " F10 Apply  F1 Help  PgUp/PgDn Ends  Esc Exit", ATTR_TITLE);
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
	baseline = settings;
	clock_probe(&card, &clock_info);
	clock_mhz = clock_get_mhz(&clock_info);
	set_status("Ready.");
	return 0;
}

static void move_selection(int direction)
{
	int n = page_count[page];
	int next = selected;

	do {
		next = (next + direction + n) % n;
	} while (!item_editable(page_items[page][next]) && next != selected);
	selected = next;
}

static void switch_page(void)
{
	page = page ? 0 : 1;
	selected = 0;
	if (!item_editable(page_items[page][0]))
		move_selection(1);
}

static void change_value(int direction)
{
	int item = page_items[page][selected];

	if (!item_editable(item))
		return;

	switch (item) {
	case ITEM_BACKLIGHT:
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
	case ITEM_DOS43:
		settings.dos43 = !settings.dos43;
		break;
	case ITEM_FILTER:
		/* just re-point at the target family; each family keeps its
		 * own params in filter_p1[]/filter_p2[] */
		settings.filter_family = (settings.filter_family + direction
			+ MXM_FIR_FAM_COUNT) % MXM_FIR_FAM_COUNT;
		break;
	case ITEM_FILTER_P1: {
		int fam = settings.filter_family;
		settings.filter_p1[fam] = fir_ui_clamp_p1(fam,
			settings.filter_p1[fam] + direction);
		break;
	}
	case ITEM_FILTER_P2: {
		int fam = settings.filter_family;
		settings.filter_p2[fam] = fir_ui_clamp_p2(fam,
			settings.filter_p2[fam] + direction);
		break;
	}
	case ITEM_CONTRAST:
		settings.contrast += direction * 5;
		if (settings.contrast < 0)
			settings.contrast = 0;
		if (settings.contrast > 100)
			settings.contrast = 100;
		break;
	case ITEM_PEAKING:
		settings.peaking += direction * 5;
		if (settings.peaking < 0)
			settings.peaking = 0;
		if (settings.peaking > 30)
			settings.peaking = 30;
		break;
	case ITEM_RGB_R:
		settings.rgb_r += direction * 5;
		if (settings.rgb_r < 0)
			settings.rgb_r = 0;
		if (settings.rgb_r > 100)
			settings.rgb_r = 100;
		break;
	case ITEM_RGB_G:
		settings.rgb_g += direction * 5;
		if (settings.rgb_g < 0)
			settings.rgb_g = 0;
		if (settings.rgb_g > 100)
			settings.rgb_g = 100;
		break;
	case ITEM_RGB_B:
		settings.rgb_b += direction * 5;
		if (settings.rgb_b < 0)
			settings.rgb_b = 0;
		if (settings.rgb_b > 100)
			settings.rgb_b = 100;
		break;
	}
}

/* PgUp/PgDn jump the selected slider to the top/bottom of its range */
static void change_extreme(int to_max)
{
	int item = page_items[page][selected];

	if (!item_editable(item))
		return;

	switch (item) {
	case ITEM_BACKLIGHT:
		settings.brightness = to_max ? 100 : 10;
		break;
	case ITEM_VCORE:
		settings.vcore_deci = to_max ? 31 : 25;
		break;
	case ITEM_CLOCK:
		clock_mhz = to_max ? MXM_CLOCK_MAX_MHZ : MXM_CLOCK_MIN_MHZ;
		break;
	case ITEM_FILTER:
		settings.filter_family = to_max ? (MXM_FIR_FAM_COUNT - 1) : 0;
		break;
	case ITEM_FILTER_P1:
		settings.filter_p1[settings.filter_family] = to_max
			? fir_ui_p1_max(settings.filter_family) : 0;
		break;
	case ITEM_FILTER_P2:
		settings.filter_p2[settings.filter_family] = to_max
			? fir_ui_p2_max(settings.filter_family) : 0;
		break;
	case ITEM_CONTRAST:
		settings.contrast = to_max ? 100 : 0;
		break;
	case ITEM_PEAKING:
		settings.peaking = to_max ? 30 : 0;
		break;
	case ITEM_RGB_R:
		settings.rgb_r = to_max ? 100 : 0;
		break;
	case ITEM_RGB_G:
		settings.rgb_g = to_max ? 100 : 0;
		break;
	case ITEM_RGB_B:
		settings.rgb_b = to_max ? 100 : 0;
		break;
	default:
		break;   /* FB / Blank / DOS43: no range ends */
	}
}

static void apply(void)
{
	set_status("Applying settings...");
	draw();
	if (mxm_write_settings(&card, &settings, &baseline)) {
		set_status(mxm_last_error());
		return;
	}
	if (clock_info.available && clock_set_mhz(&clock_info, clock_mhz)) {
		set_status("Card settings applied, but clock programming failed.");
		return;
	}
	baseline = settings;
	set_status("Settings applied. Framebuffer size changes after reboot.");
}

static void help(void)
{
	int item = page_items[page][selected];
	const char *title = "Help";
	const char *l1 = "";
	const char *l2 = "";
	const char *l3 = "";
	int row;

	switch (item) {
	case ITEM_VCORE:
		title = "Core Voltage";
		l1 = "GPU core voltage. 2.6 V is recommended for";
		l2 = "M4800/Napalm, 2.5 V for M3800/Avenger.";
		l3 = "Applies on Apply (F10); use carefully.";
		break;
	case ITEM_CLOCK:
		title = "Core/Memory Clock";
		l1 = "GPU core/memory clock in MHz. Applies on Apply";
		l2 = "and is NOT stored on the card.";
		l3 = "Needs the 3dfx driver interface (real mode).";
		break;
	case ITEM_FB:
		title = "Framebuffer Memory";
		l1 = "Framebuffer size reserved on the card. Stored";
		l2 = "on the card; takes effect after a reboot.";
		l3 = "Avoid 64 MB under Windows 98.";
		break;
	case ITEM_BLANK:
		title = "VSA NT Blank Fix";
		l1 = "Shifts the VSA output right to work around an NT";
		l2 = "blanking quirk; the onboard FPGA compensates.";
		l3 = "Stored on the card; needs a mode change.";
		break;
	case ITEM_BACKLIGHT:
		title = "Panel Backlight";
		l1 = "Panel backlight level (10-100%).";
		l2 = "Applies on Apply. Stored on the card.";
		break;
	case ITEM_DOS43:
		title = "Force 4:3 for DOS modes";
		l1 = "Pillarboxes narrow DOS modes to their 4:3 shape";
		l2 = "instead of stretching to fill the panel.";
		l3 = "Stored on the card.";
		break;
	case ITEM_FILTER:
		title = "Scaling Filter";
		l1 = "Kernel used to upscale to the panel. Mitchell is";
		l2 = "a good default; Keys sharper; Gaussian soft;";
		l3 = "Lanczos-2 sharpest but rings the most.";
		break;
	case ITEM_FILTER_P1:
	case ITEM_FILTER_P2:
		title = "Filter Parameter";
		l1 = "Shapes the scaling filter; each family keeps its";
		l2 = "own. Lower/negative = sharper with ringing,";
		l3 = "higher = softer. PgUp/PgDn jump to the ends.";
		break;
	case ITEM_CONTRAST:
		title = "Contrast";
		l1 = "Scaler contrast (0-100).";
		l2 = "Applies live on the card.";
		break;
	case ITEM_PEAKING:
		title = "Edge Enhance";
		l1 = "Edge enhancement / sharpening (0-30, 0 = off).";
		l2 = "Adds bite on top of the scaling filter.";
		break;
	default:   /* White Balance R/G/B */
		title = "White Balance";
		l1 = "Per-channel white balance (0-100, 50 = neutral).";
		l2 = "Lower a channel to warm or cool the image.";
		l3 = "All three channels apply together.";
		break;
	}

	/* clear the area behind the popup first, then frame + text */
	for (row = 7; row <= 17; row++)
		fill(9, row, 62, ' ', ATTR_NORMAL);
	box(9, 7, 62, 11, title);
	field(12, 10, 56, l1, ATTR_NORMAL);
	field(12, 11, 56, l2, ATTR_NORMAL);
	field(12, 12, 56, l3, ATTR_NORMAL);
	field(12, 15, 56, "Press any key to return.", ATTR_TITLE);
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
			case 73:   /* PageUp -> range max */
				change_extreme(1);
				break;
			case 81:   /* PageDown -> range min */
				change_extreme(0);
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
		} else if (key == 9) {
			switch_page();
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
