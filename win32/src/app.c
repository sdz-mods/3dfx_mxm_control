#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>

#include "io_backend.h"
#include "hwc_ext.h"
#include "mxm_protocol.h"
#include "fir_ui.h"
#include "mxm.h"
#include "resource.h"

#define APP_VERSION "1.2"
#define APP_TITLE "3dfx MXM Control " APP_VERSION
#define WM_TRAYICON (WM_USER + 100)
#define WM_APP_REFRESH (WM_USER + 101)

enum {
	ID_REFRESH = 1001,
	ID_APPLY,
	ID_DEFAULTS,
	ID_EXIT,
	ID_BRIGHTNESS,
	ID_VCORE,
	ID_CLOCK,
	ID_FB,
	ID_BLANK,
	ID_DOS43,
	ID_FILTER,
	ID_FILTER_P1,
	ID_FILTER_P2,
	ID_CONTRAST,
	ID_PEAKING,
	ID_RGB_R,
	ID_RGB_G,
	ID_RGB_B,
	ID_TRAY_OPEN,
	ID_TRAY_REFRESH,
	ID_TRAY_APPLY,
	ID_TRAY_DEFAULTS,
	ID_TRAY_EXIT,
	ID_HELP_CONTENTS,
	ID_HELP_ABOUT,
};

static HINSTANCE g_inst;
static HWND g_hwnd;
static HFONT g_font;
static HICON g_icon;
static HICON g_tray_icon;
static int g_tray_visible;
static int g_io_ready;
static mxm_card_t g_card;
static mxm_settings_t g_settings;
static mxm_settings_t g_baseline;   /* last-known card state (for change detection) */
static int g_pending_autostart_refresh;  /* autostarted: re-read on first tray open */
static DWORD g_msp_idle_at;   /* tick when the MSP read state resets after a short read */

static void refresh_card(void);
static void refresh_status(void);

/*
 * A short status read stops the XIO transfer early, leaving the MSP's read
 * state mid-block until its idle timeout. Block here until that has elapsed so
 * the next full read/write can't desync. Only ever waits if the user acts
 * within ~2 s of a first-open status refresh, which is rare.
 */
static void msp_wait_idle(void)
{
	if (g_msp_idle_at) {
		DWORD now = GetTickCount();
		if ((long)(g_msp_idle_at - now) > 0)
			Sleep(g_msp_idle_at - now);
		g_msp_idle_at = 0;
	}
}
static hwc_ext_info_t g_hwc;
static int g_clock_mhz = MXM_CLOCK_DEFAULT_MHZ;

static HWND g_tab;
#define MAX_TAB_CTRLS 40
static HWND g_tab_ctrl[2][MAX_TAB_CTRLS];
static int g_tab_n[2];

static HWND lbl_backend;
static HWND lbl_card;
static HWND lbl_gpu;
static HWND lbl_revision;
static HWND lbl_display;
static HWND lbl_bridge;
static HWND lbl_brightness;
static HWND lbl_vcore;
static HWND lbl_clock;
static HWND lbl_fb;
static HWND lbl_blank;
static HWND lbl_gpu_temp;
static HWND lbl_smc_temp;
static HWND lbl_fan;
static HWND lbl_help;
static HWND lbl_status;
static HWND lbl_hwc;
static HWND trk_brightness;
static HWND trk_vcore;
static HWND trk_clock;
static HWND cbo_fb;
static HWND chk_blank;
static HWND btn_refresh;
static HWND btn_apply;
static HWND btn_defaults;
static HWND chk_dos43;
static HWND lbl_filter;
static HWND cbo_filter;
static HWND lbl_p1;
static HWND trk_p1;
static HWND lbl_p2;
static HWND trk_p2;
static HWND lbl_contrast;
static HWND trk_contrast;
static HWND lbl_peaking;
static HWND trk_peaking;
static HWND lbl_rgb_r;
static HWND trk_rgb_r;
static HWND lbl_rgb_g;
static HWND trk_rgb_g;
static HWND lbl_rgb_b;
static HWND trk_rgb_b;
static HWND lbl_scaler_status;
static HWND lbl_scaler_res;

static HWND make_control(const char *cls, const char *text, DWORD style,
			 int x, int y, int w, int h, int id)
{
	HWND hwnd = CreateWindowExA(0, cls, text, style | WS_CHILD | WS_VISIBLE,
				   x, y, w, h, g_hwnd, (HMENU)id, g_inst, NULL);
	if (hwnd && g_font)
		SendMessage(hwnd, WM_SETFONT, (WPARAM)g_font, TRUE);
	return hwnd;
}

static HWND make_label(const char *text, int x, int y, int w, int h)
{
	return make_control("STATIC", text, 0, x, y, w, h, 0);
}

static HWND make_group(const char *text, int x, int y, int w, int h)
{
	return make_control("BUTTON", text, BS_GROUPBOX, x, y, w, h, 0);
}

static HWND make_hline(int x, int y, int w)
{
	return make_control("STATIC", "", SS_ETCHEDHORZ, x, y, w, 2, 0);
}

static void tab_add(int tab, HWND h)
{
	if (h && g_tab_n[tab] < MAX_TAB_CTRLS)
		g_tab_ctrl[tab][g_tab_n[tab]++] = h;
}

static void tab_select(int tab)
{
	int i;

	for (i = 0; i < g_tab_n[0]; i++)
		ShowWindow(g_tab_ctrl[0][i], tab == 0 ? SW_SHOW : SW_HIDE);
	for (i = 0; i < g_tab_n[1]; i++)
		ShowWindow(g_tab_ctrl[1][i], tab == 1 ? SW_SHOW : SW_HIDE);
}

static HWND make_tab_label(int tab, const char *text, int x, int y, int w, int h)
{
	HWND l = make_label(text, x, y, w, h);
	tab_add(tab, l);
	return l;
}

static HWND make_tab_group(int tab, const char *text, int x, int y, int w, int h)
{
	HWND g = make_group(text, x, y, w, h);
	tab_add(tab, g);
	return g;
}

static HWND make_tab_hline(int tab, int x, int y, int w)
{
	HWND l = make_hline(x, y, w);
	tab_add(tab, l);
	return l;
}

static HWND make_tab_vline(int tab, int x, int y, int h)
{
	HWND l = make_control("STATIC", "", SS_ETCHEDVERT, x, y, 2, h, 0);
	tab_add(tab, l);
	return l;
}

/*
 * Trackbar subclass: PageUp/PageDown jump to the top/bottom of the range
 * (instead of the native page step), matching the tuning console and the DOS
 * and BIOS setup screens. All sliders share one original class proc.
 */
static WNDPROC g_trk_proc;

static LRESULT CALLBACK trk_subproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
	if (msg == WM_KEYDOWN && (wp == VK_PRIOR || wp == VK_NEXT)) {
		int hi = (int)SendMessage(h, TBM_GETRANGEMAX, 0, 0);
		int lo = (int)SendMessage(h, TBM_GETRANGEMIN, 0, 0);

		SendMessage(h, TBM_SETPOS, TRUE, wp == VK_PRIOR ? hi : lo);
		SendMessage(GetParent(h), WM_HSCROLL, 0, (LPARAM)h);
		return 0;
	}
	return CallWindowProc(g_trk_proc, h, msg, wp, lp);
}

/* label + trackbar with the original shared geometry, registered on a tab */
static HWND make_slider(int tab, int y, int id, int lo, int hi, int tick,
			HWND *out_label)
{
	HWND lbl = make_label("", 28, y + 8, 210, 18);
	HWND trk = make_control(TRACKBAR_CLASSA, "", WS_TABSTOP | TBS_AUTOTICKS,
				250, y, 260, 40, id);
	WNDPROC prev;

	SendMessage(trk, TBM_SETRANGE, TRUE, MAKELONG(lo, hi));
	SendMessage(trk, TBM_SETTICFREQ, tick, 0);
	prev = (WNDPROC)SetWindowLongPtr(trk, GWLP_WNDPROC, (LONG_PTR)trk_subproc);
	if (!g_trk_proc)
		g_trk_proc = prev;
	tab_add(tab, lbl);
	tab_add(tab, trk);
	if (out_label)
		*out_label = lbl;
	return trk;
}

/*
 * Push the active family's stored params into the filter combobox and the P1/P2
 * sliders, reconfiguring the slider ranges for that family. P2 (the Mitchell "C"
 * term) is disabled for families that do not use it. The per-family store lives
 * in g_settings.filter_p1[]/filter_p2[], read back from and written to the card.
 */
static void configure_filter_controls(void)
{
	int fam = g_settings.filter_family;
	int p1max, p2max;

	if (fam < 0 || fam >= MXM_FIR_FAM_COUNT)
		fam = MXM_FIR_FAM_MITCHELL;
	p1max = fir_ui_p1_max(fam);
	p2max = fir_ui_p2_max(fam);

	g_settings.filter_family = fam;
	g_settings.filter_p1[fam] = fir_ui_clamp_p1(fam, g_settings.filter_p1[fam]);
	g_settings.filter_p2[fam] = fir_ui_clamp_p2(fam, g_settings.filter_p2[fam]);

	SendMessage(cbo_filter, CB_SETCURSEL, fam, 0);

	SendMessage(trk_p1, TBM_SETRANGE, TRUE, MAKELONG(0, p1max ? p1max : 1));
	SendMessage(trk_p1, TBM_SETTICFREQ, 4, 0);
	SendMessage(trk_p1, TBM_SETPOS, TRUE, g_settings.filter_p1[fam]);
	EnableWindow(trk_p1, p1max > 0 && g_settings.scaler_capable);

	SendMessage(trk_p2, TBM_SETRANGE, TRUE, MAKELONG(0, p2max ? p2max : 1));
	SendMessage(trk_p2, TBM_SETTICFREQ, 4, 0);
	SendMessage(trk_p2, TBM_SETPOS, TRUE, g_settings.filter_p2[fam]);
	EnableWindow(trk_p2, p2max > 0 && g_settings.scaler_capable);
}

static void set_text(HWND hwnd, const char *fmt, ...)
{
	char buf[256];
	va_list ap;

	va_start(ap, fmt);
	wvsprintfA(buf, fmt, ap);
	va_end(ap);
	SetWindowTextA(hwnd, buf);
}

static void set_status(const char *fmt, ...)
{
	char buf[256];
	va_list ap;

	va_start(ap, fmt);
	wvsprintfA(buf, fmt, ap);
	va_end(ap);
	SetWindowTextA(lbl_status, buf);
}

static void set_help_text(const char *text)
{
	SetWindowTextA(lbl_help, text);
}

static void show_help_contents(void)
{
	MessageBoxA(g_hwnd,
		"3dfx MXM Control reads and writes settings stored on the MXM card.\r\n"
		"Settings are grouped on two tabs.\r\n\r\n"
		"CARD INFO & SETTINGS TAB\r\n"
		"Recommended Core Voltage is 2.6 V for M4800/Napalm and 2.5 V for "
		"M3800/Avenger.\r\n"
		"Panel Backlight and Core Voltage apply immediately after Apply.\r\n"
		"Core/Memory Clock applies immediately after Apply and is not stored "
		"on the card.\r\n"
		"Framebuffer Memory is stored on the card and takes effect after reboot. "
		"64 MB is not recommended for Windows 98.\r\n"
		"VSA NT Blank Fix is stored on the card and takes effect after a "
		"resolution change or display mode reset.\r\n\r\n"
		"IMAGE & PANEL TAB\r\n"
		"Force 4:3 for DOS modes pillarboxes DOS-era video modes - both text "
		"modes and graphics modes (e.g. 640x400, 640x350, 720x350, 720x400) - "
		"so they display in their intended 4:3 shape instead of stretched to "
		"fill the panel. Stored on the card.\r\n"
		"Scaling Filter chooses the kernel used to upscale to the panel and its "
		"shape parameters (default Mitchell B=0.40 C=0.55). Lower/negative values "
		"sharpen with more ringing; higher values soften.\r\n"
		"Edge Enhance (0-30, 0 = off) adds extra bite on top of the filter.\r\n"
		"Contrast and White Balance R/G/B apply live; White Balance 50 is "
		"neutral. Lower a channel to warm or cool the image.\r\n"
		"On any slider, PgUp/PgDn jumps to the ends of the range.\r\n\r\n"
		"Defaults loads safe values into the utility. Click Apply to store them "
		"on the card.\r\n\r\n"
		"If video output is lost after a bad Core Voltage setting (the one "
		"setting that can realistically cause this), use the BIOS setup recovery "
		"option or spam R during SeaBIOS startup to restore defaults.",
		"3dfx MXM Control Help", MB_OK | MB_ICONINFORMATION);
}

static void show_about(void)
{
	MessageBoxA(g_hwnd,
		"3dfx MXM Control " APP_VERSION "\r\n\r\n"
		"Unified native Win32 control panel for 3dfx M3800 and M4800 MXM cards.\r\n\r\n"
		"Supports Windows 98 and Windows XP.",
		"About 3dfx MXM Control", MB_OK | MB_ICONINFORMATION);
}

static void tray_update(int add)
{
	NOTIFYICONDATAA nid;

	memset(&nid, 0, sizeof(nid));
	nid.cbSize = sizeof(nid);
	nid.hWnd = g_hwnd;
	nid.uID = 1;
	nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
	nid.uCallbackMessage = WM_TRAYICON;
	nid.hIcon = g_tray_icon ? g_tray_icon : g_icon;
	lstrcpynA(nid.szTip, APP_TITLE, sizeof(nid.szTip));

	if (add) {
		if (!g_tray_visible)
			Shell_NotifyIconA(NIM_ADD, &nid);
		g_tray_visible = 1;
	} else {
		if (g_tray_visible)
			Shell_NotifyIconA(NIM_DELETE, &nid);
		g_tray_visible = 0;
	}
}

static void show_main_window(void)
{
	tray_update(0);
	ShowWindow(g_hwnd, SW_SHOW);
	ShowWindow(g_hwnd, SW_RESTORE);
	SetForegroundWindow(g_hwnd);

	/*
	 * When the app autostarts at logon, the initial read happens while the
	 * OS may not be driving a valid video signal yet, so the scaler reports
	 * "no signal". Re-read once the first time the user opens the window.
	 * Paint the window synchronously first, then post the (slow) read so it
	 * displays immediately instead of blocking on the bit-banged transfer.
	 */
	if (g_pending_autostart_refresh) {
		g_pending_autostart_refresh = 0;
		RedrawWindow(g_hwnd, NULL, NULL,
			     RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
		PostMessageA(g_hwnd, WM_APP_REFRESH, 0, 0);
	}
}

static void hide_to_tray(void)
{
	ShowWindow(g_hwnd, SW_HIDE);
	tray_update(1);
}

static void enable_card_controls(int enabled)
{
	int scaler = enabled && g_settings.scaler_capable;

	EnableWindow(btn_apply, enabled);
	EnableWindow(btn_defaults, enabled);
	EnableWindow(trk_brightness, enabled);
	EnableWindow(trk_vcore, enabled);
	EnableWindow(trk_clock, enabled && g_hwc.available);
	EnableWindow(cbo_fb, enabled && g_card.type == MXM_CARD_M4800);
	EnableWindow(chk_blank, enabled && g_card.type == MXM_CARD_M4800);

	EnableWindow(chk_dos43, scaler);
	EnableWindow(cbo_filter, scaler);
	EnableWindow(trk_p1, scaler);
	EnableWindow(trk_p2, scaler && fir_ui_p2_max(g_settings.filter_family) > 0);
	EnableWindow(trk_contrast, scaler);
	EnableWindow(trk_peaking, scaler);
	EnableWindow(trk_rgb_r, scaler);
	EnableWindow(trk_rgb_g, scaler);
	EnableWindow(trk_rgb_b, scaler);
}

static void update_value_labels(void)
{
	set_text(lbl_brightness, "Panel Backlight: %d%%", g_settings.brightness);
	set_text(lbl_vcore, "Core Voltage: %d.%d V",
		 g_settings.vcore_deci / 10, g_settings.vcore_deci % 10);
	set_text(lbl_clock, "%s Clock: %d MHz",
		 g_card.type == MXM_CARD_M3800 ? "Avenger Core/Memory" :
		 "VSA-100 Core/Memory", g_clock_mhz);
	set_text(lbl_fb, "Framebuffer Memory: %d MB", g_settings.fb_mb);
	set_text(lbl_blank, "%s: %s",
		 g_card.type == MXM_CARD_M3800 ? "Avenger Blank Fix" : "VSA NT Blank Fix",
		 g_settings.blank_fix ? "Enabled" : "Disabled");
	set_text(lbl_gpu_temp, "GPU Temperature: %d C", g_settings.gpu_temp);
	set_text(lbl_smc_temp, "SMC Temperature: %d C", g_settings.smc_temp);
	set_text(lbl_fan, "Fan Speed: %d%%", g_settings.fan_speed);

	{
		int fam = g_settings.filter_family;
		char v1[8];
		char v2[8];

		if (fam < 0 || fam >= MXM_FIR_FAM_COUNT)
			fam = MXM_FIR_FAM_MITCHELL;
		if (fir_ui_p1_max(fam) > 0) {
			fir_ui_fmt_x100(fir_ui_p1_val_x100(fam, g_settings.filter_p1[fam]), v1);
			set_text(lbl_p1, "Filter %s: %s", fir_ui_p1_label(fam), v1);
		} else {
			SetWindowTextA(lbl_p1, "Filter: fixed kernel (no parameters)");
		}
		if (fir_ui_p2_max(fam) > 0) {
			fir_ui_fmt_x100(fir_ui_p2_val_x100(fam, g_settings.filter_p2[fam]), v2);
			set_text(lbl_p2, "Filter %s: %s", fir_ui_p2_label(fam), v2);
		} else {
			SetWindowTextA(lbl_p2, "");
		}
	}
	set_text(lbl_contrast, "Contrast: %d", g_settings.contrast);
	set_text(lbl_peaking, "Edge Enhance: %d", g_settings.peaking);
	set_text(lbl_rgb_r, "White balance R: %d", g_settings.rgb_r);
	set_text(lbl_rgb_g, "White balance G: %d", g_settings.rgb_g);
	set_text(lbl_rgb_b, "White balance B: %d", g_settings.rgb_b);

	if (!g_settings.scaler_capable) {
		SetWindowTextA(lbl_scaler_status,
			       "Scaler: requires MSP firmware v2 (not detected)");
		SetWindowTextA(lbl_scaler_res, "");
	} else if (!g_settings.scaler_link) {
		SetWindowTextA(lbl_scaler_status, "Scaler: no UART link");
		SetWindowTextA(lbl_scaler_res, "");
	} else {
		set_text(lbl_scaler_status, "Scaler: linked, %s",
			 g_settings.scaler_lock ? "signal locked" : "no signal");
		if (g_settings.scaler_lock)
			set_text(lbl_scaler_res, "Input resolution: %d x %d",
				 g_settings.in_width, g_settings.in_height);
		else
			SetWindowTextA(lbl_scaler_res, "Input resolution: -");
	}
}

static void update_default_help(void)
{
	set_help_text("Select a setting to see notes about when it applies and how it is stored.");
}

static void show_setting_help(int id)
{
	switch (id) {
	case ID_BRIGHTNESS:
		set_help_text("Panel Backlight applies immediately after Apply and is stored on the MXM card.");
		break;
	case ID_VCORE:
		set_help_text("Core Voltage applies immediately after Apply. Use carefully; Defaults restores safe values.");
		break;
	case ID_CLOCK:
		set_help_text("Core/Memory Clock applies immediately after Apply.");
		break;
	case ID_FB:
		set_help_text("Framebuffer Memory is stored on the MXM card and takes effect after reboot. Avoid 64 MB on Windows 98.");
		break;
	case ID_BLANK:
		set_help_text("Blank Fix is stored on the MXM card and takes effect after a resolution change or display mode reset.");
		break;
	case ID_DOS43:
		set_help_text("Force 4:3 pillarboxes DOS-era video modes - both text modes and graphics modes (e.g. 640x400, 640x350, 720x350, 720x400) - so they display in their intended 4:3 shape instead of stretched to fill the panel. Stored on the card.");
		break;
	case ID_FILTER:
		set_help_text("Scaling filter family used to upscale to the panel. Mitchell is a good all-round default; Keys is a sharper cubic; Gaussian is soft; Lanczos-2 is sharpest but rings the most (no parameters). Applies live. PgUp/PgDn on any slider jumps to the ends.");
		break;
	case ID_FILTER_P1:
	case ID_FILTER_P2:
		set_help_text("Filter shape parameter. Lower/negative = sharper with more ringing; higher = softer. Mitchell B=0.40 C=0.55 is the default. Applies live.");
		break;
	case ID_CONTRAST:
		set_help_text("Scaler contrast (0-100). Applies live to the displayed image.");
		break;
	case ID_PEAKING:
		set_help_text("Edge enhancement (0-30, 0 = off). Adds extra bite on top of the scaling filter. Values above ~15 start to look harsh. Applies live.");
		break;
	case ID_RGB_R:
	case ID_RGB_G:
	case ID_RGB_B:
		set_help_text("White balance gain per channel (0-100, 50 = neutral). Lower a channel to warm or cool the image. Applies live.");
		break;
	default:
		update_default_help();
		break;
	}
}

static void settings_to_controls(void)
{
	SendMessage(cbo_fb, CB_RESETCONTENT, 0, 0);
	if (g_card.type == MXM_CARD_M4800) {
		SendMessage(cbo_fb, CB_ADDSTRING, 0, (LPARAM)"32 MB");
		SendMessage(cbo_fb, CB_ADDSTRING, 0, (LPARAM)"64 MB");
	} else {
		SendMessage(cbo_fb, CB_ADDSTRING, 0, (LPARAM)"16 MB");
	}

	SendMessage(trk_brightness, TBM_SETPOS, TRUE, g_settings.brightness);
	SendMessage(trk_vcore, TBM_SETPOS, TRUE, g_settings.vcore_deci);
	SendMessage(trk_clock, TBM_SETPOS, TRUE, g_clock_mhz);

	if (g_card.type == MXM_CARD_M4800)
		SendMessage(cbo_fb, CB_SETCURSEL, g_settings.fb_mb == 64 ? 1 : 0, 0);
	else
		SendMessage(cbo_fb, CB_SETCURSEL, 0, 0);
	SendMessage(chk_blank, BM_SETCHECK,
		    g_settings.blank_fix ? BST_CHECKED : BST_UNCHECKED, 0);

	SendMessage(chk_dos43, BM_SETCHECK,
		    g_settings.dos43 ? BST_CHECKED : BST_UNCHECKED, 0);
	configure_filter_controls();
	SendMessage(trk_contrast, TBM_SETPOS, TRUE, g_settings.contrast);
	SendMessage(trk_peaking, TBM_SETPOS, TRUE, g_settings.peaking);
	SendMessage(trk_rgb_r, TBM_SETPOS, TRUE, g_settings.rgb_r);
	SendMessage(trk_rgb_g, TBM_SETPOS, TRUE, g_settings.rgb_g);
	SendMessage(trk_rgb_b, TBM_SETPOS, TRUE, g_settings.rgb_b);
	update_value_labels();
}

static void controls_to_settings(void)
{
	int fb_sel;

	g_settings.brightness = (int)SendMessage(trk_brightness, TBM_GETPOS, 0, 0);
	g_settings.vcore_deci = (int)SendMessage(trk_vcore, TBM_GETPOS, 0, 0);
	g_clock_mhz = (int)SendMessage(trk_clock, TBM_GETPOS, 0, 0);
	fb_sel = (int)SendMessage(cbo_fb, CB_GETCURSEL, 0, 0);
	if (g_card.type == MXM_CARD_M4800)
		g_settings.fb_mb = fb_sel == 1 ? 64 : 32;
	else
		g_settings.fb_mb = 16;
	g_settings.blank_fix = g_card.type == MXM_CARD_M4800
		? SendMessage(chk_blank, BM_GETCHECK, 0, 0) == BST_CHECKED : 1;

	if (g_settings.scaler_capable) {
		int fam = (int)SendMessage(cbo_filter, CB_GETCURSEL, 0, 0);

		g_settings.dos43 =
			SendMessage(chk_dos43, BM_GETCHECK, 0, 0) == BST_CHECKED;
		if (fam < 0 || fam >= MXM_FIR_FAM_COUNT)
			fam = MXM_FIR_FAM_MITCHELL;
		g_settings.filter_family = fam;
		g_settings.filter_p1[fam] = fir_ui_clamp_p1(fam,
			(int)SendMessage(trk_p1, TBM_GETPOS, 0, 0));
		g_settings.filter_p2[fam] = fir_ui_clamp_p2(fam,
			(int)SendMessage(trk_p2, TBM_GETPOS, 0, 0));
		g_settings.contrast = (int)SendMessage(trk_contrast, TBM_GETPOS, 0, 0);
		g_settings.peaking = (int)SendMessage(trk_peaking, TBM_GETPOS, 0, 0);
		g_settings.rgb_r = (int)SendMessage(trk_rgb_r, TBM_GETPOS, 0, 0);
		g_settings.rgb_g = (int)SendMessage(trk_rgb_g, TBM_GETPOS, 0, 0);
		g_settings.rgb_b = (int)SendMessage(trk_rgb_b, TBM_GETPOS, 0, 0);
	}
	update_value_labels();
}

static void update_card_labels(void)
{
	set_text(lbl_backend, "I/O Backend: %s", io_backend_name());
	if (!g_card.present) {
		SetWindowTextA(lbl_card, "Card Model: No supported card detected");
		SetWindowTextA(lbl_gpu, "GPU Model: Unknown");
		SetWindowTextA(lbl_revision, "Card Revision: Unknown");
		SetWindowTextA(lbl_display, "Display Type: Unknown");
		SetWindowTextA(lbl_bridge, "Bridge: Unknown");
		return;
	}

	set_text(lbl_card, "Card Model: %s", g_card.model);
	set_text(lbl_gpu, "GPU Model: %s", g_card.gpu);
	set_text(lbl_revision, "Card Revision: %s", g_card.revision);
	set_text(lbl_display, "Display Type: %s", g_card.display);
	set_text(lbl_bridge, "Bridge: %02x:%02x.%u  SSID: %04x",
		 g_card.bridge.bus, g_card.bridge.dev, g_card.bridge.func, g_card.ssid);
}

static void refresh_card(void)
{
	msp_wait_idle();
	enable_card_controls(0);
	mxm_card_clear(&g_card);
	hwc_ext_probe(g_hwnd, &g_hwc);
	if (g_hwc.available)
		g_clock_mhz = hwc_ext_pll_to_mhz(g_hwc.pllctrl1);
	if (lbl_hwc)
		SetWindowTextA(lbl_hwc, g_hwc.status);

	if (!g_io_ready) {
		set_status("I/O backend is not initialized.");
		update_card_labels();
		return;
	}

	if (mxm_detect(&g_card)) {
		update_card_labels();
		set_status("%s", mxm_last_error());
		return;
	}

	update_card_labels();
	if (mxm_read_settings(&g_card, &g_settings)) {
		set_status("%s", mxm_last_error());
		return;
	}
	g_baseline = g_settings;

	settings_to_controls();
	enable_card_controls(1);
	SetWindowTextA(lbl_hwc, g_hwc.status);
	set_status("Ready.");
}

/*
 * Lightweight re-read for the first tray open after autostart. The card was
 * already detected at startup, so skip the PCI re-detection and the driver
 * probe and just re-read the settings/status block (which carries the live
 * scaler link, lock and input resolution). Falls back to a full refresh if
 * the card was not detected at startup.
 */
static void refresh_status(void)
{
	if (!g_io_ready || !g_card.present) {
		refresh_card();
		return;
	}
	if (mxm_read_status(&g_card, &g_settings)) {
		set_status("%s", mxm_last_error());
		return;
	}
	/* short read leaves the MSP mid-block; guard the next transaction */
	g_msp_idle_at = GetTickCount() + 2100;
	update_value_labels();
	set_status("Ready.");
}

static void apply_settings(void)
{
	mxm_settings_t desired;
	char err[160];
	int desired_clock;

	if (!g_card.present)
		return;

	controls_to_settings();
	desired = g_settings;
	desired_clock = g_clock_mhz;
	enable_card_controls(0);
	set_status("Applying settings...");
	msp_wait_idle();
	if (mxm_write_settings(&g_card, &desired, &g_baseline)) {
		set_status("%s", mxm_last_error());
		enable_card_controls(1);
		return;
	}

	if (g_hwc.available && hwc_ext_set_clock_mhz(&g_hwc, desired_clock,
						    err, sizeof(err))) {
		set_status("%s", err);
		enable_card_controls(1);
		return;
	}

	g_settings = desired;
	g_baseline = desired;   /* card now holds these; next Apply diffs against them */
	if (g_hwc.available)
		g_clock_mhz = hwc_ext_pll_to_mhz(g_hwc.pllctrl1);
	settings_to_controls();
	SetWindowTextA(lbl_hwc, g_hwc.status);
	enable_card_controls(1);
	set_status("Settings applied. Click Refresh to read back from the card.");
}

static void load_defaults(void)
{
	mxm_settings_t live;

	if (!g_card.present)
		return;

	/* keep the live/telemetry/capability fields; only reset the settings */
	live = g_settings;
	mxm_defaults(&g_card, &g_settings);
	g_clock_mhz = MXM_CLOCK_DEFAULT_MHZ;

	g_settings.proto_version = live.proto_version;
	g_settings.scaler_capable = live.scaler_capable;
	g_settings.gpu_temp = live.gpu_temp;
	g_settings.smc_temp = live.smc_temp;
	g_settings.fan_speed = live.fan_speed;
	g_settings.scaler_link = live.scaler_link;
	g_settings.scaler_lock = live.scaler_lock;
	g_settings.in_width = live.in_width;
	g_settings.in_height = live.in_height;
	memcpy(g_settings.raw, live.raw, sizeof(g_settings.raw));

	settings_to_controls();
	set_status("Defaults loaded. Click Apply to store them on the card.");
}

static void create_ui(void)
{
	HWND logo;
	TCITEMA tie;
	int i;

	g_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

	logo = make_control("STATIC", "", SS_ICON, 16, 10, 32, 32, 0);
	SendMessage(logo, STM_SETICON, (WPARAM)g_icon, 0);
	make_label("Unified M3800/M4800 control panel", 58, 20, 320, 18);

	g_tab = make_control(WC_TABCONTROLA, "", WS_TABSTOP, 8, 50, 600, 576, 0);
	memset(&tie, 0, sizeof(tie));
	tie.mask = TCIF_TEXT;
	tie.pszText = (LPSTR)"Card Info && Settings";
	SendMessage(g_tab, TCM_INSERTITEMA, 0, (LPARAM)&tie);
	tie.pszText = (LPSTR)"Image && Panel";
	SendMessage(g_tab, TCM_INSERTITEMA, 1, (LPARAM)&tie);

	/* ---- Tab 0: Card Info & Settings (original appearance, no backlight) ---- */
	make_tab_group(0, "Card Information", 16, 78, 580, 96);
	lbl_card     = make_tab_label(0, "Card Model:", 28, 102, 255, 18);
	lbl_gpu      = make_tab_label(0, "GPU Model:", 28, 126, 255, 18);
	lbl_revision = make_tab_label(0, "Card Revision:", 28, 150, 255, 18);
	make_tab_vline(0, 300, 100, 62);
	lbl_display  = make_tab_label(0, "Display Type:", 318, 102, 270, 18);
	lbl_bridge   = make_tab_label(0, "Bridge:", 318, 126, 270, 18);

	make_tab_group(0, "Live Status", 16, 182, 580, 58);
	lbl_gpu_temp = make_tab_label(0, "GPU Temperature:", 28, 204, 190, 18);
	lbl_smc_temp = make_tab_label(0, "SMC Temperature:", 228, 204, 180, 18);
	lbl_fan      = make_tab_label(0, "Fan Speed:", 418, 204, 150, 18);

	make_tab_group(0, "Driver Interface", 16, 248, 580, 58);
	lbl_hwc = make_tab_label(0, "3dfx driver interface:", 28, 270, 548, 18);

	make_tab_group(0, "Card Settings", 16, 314, 580, 186);
	trk_vcore = make_slider(0, 334, ID_VCORE, 25, 31, 1, &lbl_vcore);
	trk_clock = make_slider(0, 374, ID_CLOCK, MXM_CLOCK_MIN_MHZ,
				MXM_CLOCK_MAX_MHZ, 10, &lbl_clock);
	make_tab_hline(0, 24, 420, 552);
	lbl_fb = make_tab_label(0, "Framebuffer Memory:", 28, 440, 210, 18);
	cbo_fb = make_control("COMBOBOX", "", WS_TABSTOP | CBS_DROPDOWNLIST,
			      262, 436, 120, 100, ID_FB);
	tab_add(0, cbo_fb);
	lbl_blank = make_tab_label(0, "VSA NT Blank Fix:", 28, 474, 210, 18);
	chk_blank = make_control("BUTTON", "Enable", WS_TABSTOP | BS_AUTOCHECKBOX,
				 262, 470, 90, 22, ID_BLANK);
	tab_add(0, chk_blank);

	/* ---- Tab 1: Image & Panel (backlight + scaler) ---- */
	make_tab_group(1, "Panel", 16, 78, 580, 60);
	trk_brightness = make_slider(1, 94, ID_BRIGHTNESS, 10, 100, 10, &lbl_brightness);

	make_tab_group(1, "Image / Scaler Settings", 16, 146, 580, 414);
	chk_dos43 = make_control("BUTTON", "Force 4:3 for DOS modes",
				 WS_TABSTOP | BS_AUTOCHECKBOX, 28, 168, 420, 22,
				 ID_DOS43);
	tab_add(1, chk_dos43);
	make_tab_hline(1, 24, 198, 552);

	lbl_filter = make_tab_label(1, "Scaling Filter:", 28, 214, 210, 18);
	cbo_filter = make_control("COMBOBOX", "", WS_TABSTOP | CBS_DROPDOWNLIST,
				  250, 210, 200, 200, ID_FILTER);
	tab_add(1, cbo_filter);
	for (i = 0; i < MXM_FIR_FAM_COUNT; i++)
		SendMessage(cbo_filter, CB_ADDSTRING, 0, (LPARAM)fir_family_name[i]);

	trk_p1 = make_slider(1, 240, ID_FILTER_P1, 0, 20, 4, &lbl_p1);
	trk_p2 = make_slider(1, 280, ID_FILTER_P2, 0, 20, 4, &lbl_p2);
	make_tab_hline(1, 24, 326, 552);
	trk_contrast  = make_slider(1, 336, ID_CONTRAST, 0, 100, 10, &lbl_contrast);
	trk_peaking   = make_slider(1, 376, ID_PEAKING, 0, 30, 5, &lbl_peaking);
	make_tab_hline(1, 24, 422, 552);
	trk_rgb_r = make_slider(1, 432, ID_RGB_R, 0, 100, 10, &lbl_rgb_r);
	trk_rgb_g = make_slider(1, 472, ID_RGB_G, 0, 100, 10, &lbl_rgb_g);
	trk_rgb_b = make_slider(1, 512, ID_RGB_B, 0, 100, 10, &lbl_rgb_b);

	make_tab_group(1, "Scaler Status", 16, 568, 580, 50);
	lbl_scaler_status = make_tab_label(1, "Scaler:", 28, 588, 340, 18);
	lbl_scaler_res    = make_tab_label(1, "", 372, 588, 210, 18);

	/* ---- shared bottom ---- */
	make_group("Setting Notes", 12, 634, 600, 74);
	lbl_help = make_label("", 28, 654, 572, 46);

	btn_refresh = make_control("BUTTON", "Refresh", WS_TABSTOP | BS_PUSHBUTTON,
				   20, 716, 88, 28, ID_REFRESH);
	btn_apply = make_control("BUTTON", "Apply", WS_TABSTOP | BS_PUSHBUTTON,
				 118, 716, 88, 28, ID_APPLY);
	btn_defaults = make_control("BUTTON", "Defaults", WS_TABSTOP | BS_PUSHBUTTON,
				    216, 716, 88, 28, ID_DEFAULTS);
	make_control("BUTTON", "Exit", WS_TABSTOP | BS_PUSHBUTTON,
		     314, 716, 88, 28, ID_EXIT);

	lbl_backend = make_label("Backend:", 12, 754, 230, 18);
	lbl_status = make_label("Starting...", 252, 754, 350, 18);
	update_default_help();
	enable_card_controls(0);
	tab_select(0);
}

static void show_tray_menu(void)
{
	POINT pt;
	HMENU menu = CreatePopupMenu();

	AppendMenuA(menu, MF_STRING, ID_TRAY_OPEN, "Open");
	AppendMenuA(menu, MF_STRING, ID_TRAY_REFRESH, "Refresh");
	AppendMenuA(menu, MF_STRING, ID_TRAY_APPLY, "Apply");
	AppendMenuA(menu, MF_STRING, ID_TRAY_DEFAULTS, "Defaults");
	AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
	AppendMenuA(menu, MF_STRING, ID_TRAY_EXIT, "Exit");
	GetCursorPos(&pt);
	SetForegroundWindow(g_hwnd);
	TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, NULL);
	DestroyMenu(menu);
}

static void create_main_menu(HWND hwnd)
{
	HMENU menu = CreateMenu();
	HMENU help = CreatePopupMenu();

	AppendMenuA(help, MF_STRING, ID_HELP_CONTENTS, "&Contents");
	AppendMenuA(help, MF_SEPARATOR, 0, NULL);
	AppendMenuA(help, MF_STRING, ID_HELP_ABOUT, "&About");

	AppendMenuA(menu, MF_POPUP, (UINT_PTR)help, "&Help");
	SetMenu(hwnd, menu);
}

static int trk_setting_id(HWND h)
{
	if (h == trk_brightness) return ID_BRIGHTNESS;
	if (h == trk_vcore)      return ID_VCORE;
	if (h == trk_clock)      return ID_CLOCK;
	if (h == trk_p1)         return ID_FILTER_P1;
	if (h == trk_p2)         return ID_FILTER_P2;
	if (h == trk_contrast)   return ID_CONTRAST;
	if (h == trk_peaking)    return ID_PEAKING;
	if (h == trk_rgb_r)      return ID_RGB_R;
	if (h == trk_rgb_g)      return ID_RGB_G;
	if (h == trk_rgb_b)      return ID_RGB_B;
	return 0;
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg) {
	case WM_CREATE: {
		char err[256];

		g_hwnd = hwnd;
		g_icon = LoadIcon(g_inst, MAKEINTRESOURCE(IDI_APP));
		g_tray_icon = LoadImageA(g_inst, MAKEINTRESOURCE(IDI_APP),
					  IMAGE_ICON,
					  GetSystemMetrics(SM_CXSMICON),
					  GetSystemMetrics(SM_CYSMICON),
					  LR_DEFAULTCOLOR);
		create_main_menu(hwnd);
		InitCommonControls();
		create_ui();
		if (io_init(err, sizeof(err))) {
			g_io_ready = 0;
			set_status("%s", err);
			MessageBoxA(hwnd, err, APP_TITLE, MB_ICONERROR);
		} else {
			g_io_ready = 1;
			refresh_card();
		}
		return 0;
	}
	case WM_HSCROLL: {
		int sid = trk_setting_id((HWND)lp);
		if (sid)
			show_setting_help(sid);
		controls_to_settings();
		return 0;
	}
	case WM_NOTIFY: {
		NMHDR *hdr = (NMHDR *)lp;
		if (hdr && hdr->hwndFrom == g_tab && hdr->code == TCN_SELCHANGE) {
			tab_select((int)SendMessage(g_tab, TCM_GETCURSEL, 0, 0));
			return 0;
		}
		if (hdr && hdr->code == NM_SETFOCUS) {
			int sid = trk_setting_id(hdr->hwndFrom);
			if (sid)
				show_setting_help(sid);
		}
		break;
	}
	case WM_COMMAND:
		switch (LOWORD(wp)) {
		case ID_REFRESH:
		case ID_TRAY_REFRESH:
			refresh_card();
			return 0;
		case ID_APPLY:
		case ID_TRAY_APPLY:
			apply_settings();
			return 0;
		case ID_DEFAULTS:
		case ID_TRAY_DEFAULTS:
			load_defaults();
			return 0;
		case ID_EXIT:
		case ID_TRAY_EXIT:
			DestroyWindow(hwnd);
			return 0;
		case ID_HELP_CONTENTS:
			show_help_contents();
			return 0;
		case ID_HELP_ABOUT:
			show_about();
			return 0;
		case ID_TRAY_OPEN:
			show_main_window();
			return 0;
		case ID_FB:
			if (HIWORD(wp) == CBN_SELCHANGE || HIWORD(wp) == CBN_SETFOCUS) {
				show_setting_help(ID_FB);
				controls_to_settings();
			}
			return 0;
		case ID_BLANK:
			show_setting_help(ID_BLANK);
			controls_to_settings();
			return 0;
		case ID_DOS43:
			show_setting_help(ID_DOS43);
			controls_to_settings();
			return 0;
		case ID_FILTER:
			if (HIWORD(wp) == CBN_SELCHANGE || HIWORD(wp) == CBN_SETFOCUS) {
				show_setting_help(ID_FILTER);
				if (HIWORD(wp) == CBN_SELCHANGE) {
					int oldfam = g_settings.filter_family;
					int newfam = (int)SendMessage(cbo_filter,
								      CB_GETCURSEL, 0, 0);

					/* capture the family we are leaving into its
					 * per-family slot before switching */
					if (oldfam >= 0 && oldfam < MXM_FIR_FAM_COUNT) {
						g_settings.filter_p1[oldfam] =
							(int)SendMessage(trk_p1, TBM_GETPOS, 0, 0);
						g_settings.filter_p2[oldfam] =
							(int)SendMessage(trk_p2, TBM_GETPOS, 0, 0);
					}
					if (newfam < 0 || newfam >= MXM_FIR_FAM_COUNT)
						newfam = MXM_FIR_FAM_MITCHELL;
					g_settings.filter_family = newfam;
					configure_filter_controls();
					update_value_labels();
					enable_card_controls(g_card.present);
				}
			}
			return 0;
		}
		break;
	case WM_SIZE:
		if (wp == SIZE_MINIMIZED)
			hide_to_tray();
		return 0;
	case WM_TRAYICON:
		if (lp == WM_LBUTTONDBLCLK)
			show_main_window();
		else if (lp == WM_RBUTTONUP)
			show_tray_menu();
		return 0;
	case WM_APP_REFRESH:
		refresh_status();
		return 0;
	case WM_DESTROY:
		tray_update(0);
		io_shutdown();
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProc(hwnd, msg, wp, lp);
}

/* case-insensitive test for a "/opt" or "-opt" flag on the command line */
static int cmd_has(LPSTR cmd, const char *opt)
{
	char buf[256];

	if (!cmd)
		return 0;
	lstrcpynA(buf, cmd, sizeof(buf));
	CharLowerA(buf);
	return strstr(buf, opt) != NULL;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
	WNDCLASSA wc;
	MSG msg;
	int start_tray;

	start_tray = cmd_has(cmd, "/tray") || cmd_has(cmd, "-tray");
	g_pending_autostart_refresh = start_tray;

	g_inst = inst;
	memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = wndproc;
	wc.hInstance = inst;
	wc.hIcon = LoadIcon(inst, MAKEINTRESOURCE(IDI_APP));
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wc.lpszClassName = "TdfxMxmControlWindow";
	if (!RegisterClassA(&wc))
		return 1;

	{
		/*
		 * Size the window from the desired CLIENT area so the layout fits
		 * regardless of the OS title-bar/border height. A fixed total size
		 * clips the bottom on XP/Vista+ (taller caption) vs Win98 classic.
		 */
		DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
		RECT rc;

		rc.left = 0;
		rc.top = 0;
		rc.right = 620;   /* client width  */
		rc.bottom = 786;  /* client height */
		AdjustWindowRect(&rc, style, TRUE);   /* TRUE: window has a menu */

		g_hwnd = CreateWindowExA(0, wc.lpszClassName, APP_TITLE, style,
					 CW_USEDEFAULT, CW_USEDEFAULT,
					 rc.right - rc.left, rc.bottom - rc.top,
					 NULL, NULL, inst, NULL);
	}
	if (!g_hwnd)
		return 1;

	if (start_tray) {
		/* launched at logon: stay hidden, just show the tray icon */
		tray_update(1);
	} else {
		ShowWindow(g_hwnd, show);
		UpdateWindow(g_hwnd);
	}

	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return (int)msg.wParam;
}
