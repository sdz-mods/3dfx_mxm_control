#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stdio.h>

#include "io_backend.h"
#include "hwc_ext.h"
#include "mxm.h"
#include "resource.h"

#define APP_VERSION "1.0"
#define APP_TITLE "3dfx MXM Control " APP_VERSION
#define WM_TRAYICON (WM_USER + 100)

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
static hwc_ext_info_t g_hwc;
static int g_clock_mhz = 166;

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
static HWND lbl_raw;
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

static HWND make_vline(int x, int y, int h)
{
	return make_control("STATIC", "", SS_ETCHEDVERT, x, y, 2, h, 0);
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
		"3dfx MXM Control reads and writes settings stored on the MXM card.\r\n\r\n"
		"Recommended defaults are 2.6 V for M4800/Napalm and 2.5 V for M3800/Avenger.\r\n\r\n"
		"Panel Backlight and Core Voltage apply immediately after Apply.\r\n\r\n"
		"Core/Memory Clock applies immediately after Apply and is not stored "
		"on the MXM card.\r\n\r\n"
		"Framebuffer Memory is stored on the card, but takes effect after reboot. "
		"64 MB is not recommended for Windows 98.\r\n\r\n"
		"VSA NT Blank Fix is stored on the card. It takes effect after a resolution "
		"change or display mode reset. It compensates for the NT VSA driver blanking issue.\r\n\r\n"
		"Defaults loads safe values into the utility. Click Apply to store them on the card.\r\n\r\n"
		"If video output is lost after bad card settings, use the BIOS setup recovery "
		"option or spam R during SeaBIOS startup to restore MXM defaults.",
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
}

static void hide_to_tray(void)
{
	ShowWindow(g_hwnd, SW_HIDE);
	tray_update(1);
}

static void enable_card_controls(int enabled)
{
	EnableWindow(btn_apply, enabled);
	EnableWindow(btn_defaults, enabled);
	EnableWindow(trk_brightness, enabled);
	EnableWindow(trk_vcore, enabled);
	EnableWindow(trk_clock, enabled && g_hwc.available);
	EnableWindow(cbo_fb, enabled && g_card.type == MXM_CARD_M4800);
	EnableWindow(chk_blank, enabled && g_card.type == MXM_CARD_M4800);
}

static void raw_packet_text(char *buf, int len)
{
	wsprintfA(buf, "%02X %02X %02X %02X %02X",
		  g_settings.raw[0], g_settings.raw[1], g_settings.raw[2],
		  g_settings.raw[3], g_settings.raw[4]);
}

static void update_value_labels(void)
{
	char raw[32];

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
	raw_packet_text(raw, sizeof(raw));
	set_text(lbl_raw, "Raw Packet: %s", raw);
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

	settings_to_controls();
	enable_card_controls(1);
	SetWindowTextA(lbl_hwc, g_hwc.status);
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
	if (mxm_write_settings(&g_card, &desired)) {
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
	if (g_hwc.available)
		g_clock_mhz = hwc_ext_pll_to_mhz(g_hwc.pllctrl1);
	settings_to_controls();
	SetWindowTextA(lbl_hwc, g_hwc.status);
	enable_card_controls(1);
	set_status("Settings applied. Click Refresh to read back from the card.");
}

static void load_defaults(void)
{
	int gpu_temp;
	int smc_temp;
	int fan_speed;
	BYTE raw[5];

	if (!g_card.present)
		return;

	gpu_temp = g_settings.gpu_temp;
	smc_temp = g_settings.smc_temp;
	fan_speed = g_settings.fan_speed;
	memcpy(raw, g_settings.raw, sizeof(raw));

	mxm_defaults(&g_card, &g_settings);
	g_clock_mhz = 166;
	g_settings.gpu_temp = gpu_temp;
	g_settings.smc_temp = smc_temp;
	g_settings.fan_speed = fan_speed;
	memcpy(g_settings.raw, raw, sizeof(raw));
	settings_to_controls();
	set_status("Defaults loaded. Click Apply to store them on the card.");
}

static void create_ui(void)
{
	HWND logo;

	g_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

	logo = make_control("STATIC", "", SS_ICON, 18, 14, 32, 32, 0);
	SendMessage(logo, STM_SETICON, (WPARAM)g_icon, 0);
	make_label("Unified M3800/M4800 control panel", 60, 24, 300, 18);

	make_group("Card Information", 12, 58, 580, 114);
	lbl_card = make_label("Card Model:", 28, 82, 255, 18);
	lbl_gpu = make_label("GPU Model:", 28, 106, 255, 18);
	lbl_revision = make_label("Card Revision:", 28, 130, 255, 18);
	make_vline(300, 80, 74);
	lbl_display = make_label("Display Type:", 318, 82, 245, 18);
	lbl_bridge = make_label("Bridge:", 318, 106, 245, 18);
	lbl_raw = make_label("Raw Packet:", 318, 130, 245, 18);

	make_group("Live Status", 12, 182, 580, 58);
	lbl_gpu_temp = make_label("GPU Temperature:", 28, 204, 170, 18);
	lbl_smc_temp = make_label("SMC Temperature:", 218, 204, 170, 18);
	lbl_fan = make_label("Fan Speed:", 408, 204, 150, 18);

	make_group("Driver Interface", 12, 250, 580, 58);
	lbl_hwc = make_label("3dfx driver interface:", 28, 272, 548, 18);

	make_group("Card Settings", 12, 318, 580, 244);
	lbl_brightness = make_label("Panel Backlight:", 28, 346, 210, 18);
	trk_brightness = make_control(TRACKBAR_CLASSA, "", WS_TABSTOP | TBS_AUTOTICKS,
				     250, 338, 260, 40, ID_BRIGHTNESS);
	SendMessage(trk_brightness, TBM_SETRANGE, TRUE, MAKELONG(10, 100));
	SendMessage(trk_brightness, TBM_SETTICFREQ, 10, 0);

	lbl_vcore = make_label("Core Voltage:", 28, 386, 210, 18);
	trk_vcore = make_control(TRACKBAR_CLASSA, "", WS_TABSTOP | TBS_AUTOTICKS,
				250, 378, 260, 40, ID_VCORE);
	SendMessage(trk_vcore, TBM_SETRANGE, TRUE, MAKELONG(25, 31));
	SendMessage(trk_vcore, TBM_SETTICFREQ, 1, 0);

	lbl_clock = make_label("Core/Memory Clock:", 28, 426, 210, 18);
	trk_clock = make_control(TRACKBAR_CLASSA, "", WS_TABSTOP | TBS_AUTOTICKS,
				 250, 418, 260, 40, ID_CLOCK);
	SendMessage(trk_clock, TBM_SETRANGE, TRUE, MAKELONG(150, 220));
	SendMessage(trk_clock, TBM_SETTICFREQ, 10, 0);

	make_hline(24, 464, 552);
	lbl_fb = make_label("Framebuffer Memory:", 28, 482, 210, 18);
	cbo_fb = make_control("COMBOBOX", "", WS_TABSTOP | CBS_DROPDOWNLIST,
			      262, 478, 120, 100, ID_FB);

	lbl_blank = make_label("VSA NT Blank Fix:", 28, 516, 210, 18);
	chk_blank = make_control("BUTTON", "Enable", WS_TABSTOP | BS_AUTOCHECKBOX,
				 262, 512, 90, 22, ID_BLANK);

	make_group("Setting Notes", 12, 572, 580, 82);
	lbl_help = make_label("", 28, 596, 548, 34);

	make_hline(12, 666, 580);
	btn_refresh = make_control("BUTTON", "Refresh", WS_TABSTOP | BS_PUSHBUTTON,
				   20, 680, 88, 28, ID_REFRESH);
	btn_apply = make_control("BUTTON", "Apply", WS_TABSTOP | BS_PUSHBUTTON,
				 118, 680, 88, 28, ID_APPLY);
	btn_defaults = make_control("BUTTON", "Defaults", WS_TABSTOP | BS_PUSHBUTTON,
				    216, 680, 88, 28, ID_DEFAULTS);
	make_control("BUTTON", "Exit", WS_TABSTOP | BS_PUSHBUTTON,
		     314, 680, 88, 28, ID_EXIT);

	lbl_backend = make_label("Backend:", 12, 720, 230, 18);
	lbl_status = make_label("Starting...", 252, 720, 330, 18);
	update_default_help();
	enable_card_controls(0);
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
	case WM_HSCROLL:
		if ((HWND)lp == trk_brightness)
			show_setting_help(ID_BRIGHTNESS);
		else if ((HWND)lp == trk_vcore)
			show_setting_help(ID_VCORE);
		else if ((HWND)lp == trk_clock)
			show_setting_help(ID_CLOCK);
		controls_to_settings();
		return 0;
	case WM_NOTIFY: {
		NMHDR *hdr = (NMHDR *)lp;
		if (hdr && hdr->code == NM_SETFOCUS) {
			if (hdr->hwndFrom == trk_brightness)
				show_setting_help(ID_BRIGHTNESS);
			else if (hdr->hwndFrom == trk_vcore)
				show_setting_help(ID_VCORE);
			else if (hdr->hwndFrom == trk_clock)
				show_setting_help(ID_CLOCK);
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
	case WM_DESTROY:
		tray_update(0);
		io_shutdown();
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProc(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
	WNDCLASSA wc;
	MSG msg;

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

	g_hwnd = CreateWindowExA(0, wc.lpszClassName, APP_TITLE,
				 WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
				 WS_MINIMIZEBOX,
				 CW_USEDEFAULT, CW_USEDEFAULT, 620, 795,
				 NULL, NULL, inst, NULL);
	if (!g_hwnd)
		return 1;

	ShowWindow(g_hwnd, show);
	UpdateWindow(g_hwnd);

	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return (int)msg.wParam;
}
