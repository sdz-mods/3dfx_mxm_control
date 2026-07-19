#ifndef FIR_UI_H
#define FIR_UI_H

/*
 * Shared UI metadata for the scale-up FIR filter. Mirrors the MSP fir_gen.c so
 * the win32 and DOS apps present the exact same families, parameter ranges and
 * value mapping the SMC uses to generate the filter. Keep in sync with the SMC.
 *
 * Values are handled as integer hundredths (x100) so no float is needed in the
 * UI. Step is 0.05 (= 5 in x100 units) for every parameter.
 *
 * Include this from a single translation unit per app (the UI file).
 */

#include "mxm_protocol.h"

static const char *const fir_family_name[MXM_FIR_FAM_COUNT] = {
	"Mitchell-Netravali",
	"Keys cubic",
	"Gaussian",
	"Lanczos-2"
};

/* largest valid step index for each parameter (0 => parameter unused) */
static int fir_ui_p1_max(int fam)
{
	switch (fam) {
	case MXM_FIR_FAM_MITCHELL: return 20;   /* B     0.00..1.00 */
	case MXM_FIR_FAM_KEYS:     return 20;   /* a    -1.00..0.00 */
	case MXM_FIR_FAM_GAUSS:    return 24;   /* sigma 0.30..1.50 */
	case MXM_FIR_FAM_LANCZOS:  return 0;    /* parameterless (order = 2) */
	default:                   return 20;
	}
}

static int fir_ui_p2_max(int fam)
{
	return fam == MXM_FIR_FAM_MITCHELL ? 20 : 0;   /* C 0.00..1.00 */
}

/* short parameter name shown next to each slider */
static const char *fir_ui_p1_label(int fam)
{
	switch (fam) {
	case MXM_FIR_FAM_MITCHELL: return "B";
	case MXM_FIR_FAM_KEYS:     return "a";
	case MXM_FIR_FAM_GAUSS:    return "Sigma";
	default:                   return "P1";
	}
}

static const char *fir_ui_p2_label(int fam)
{
	return fam == MXM_FIR_FAM_MITCHELL ? "C" : "";
}

/* step index -> real parameter value, in integer hundredths */
static int fir_ui_p1_val_x100(int fam, int idx)
{
	switch (fam) {
	case MXM_FIR_FAM_MITCHELL: return    0 + idx * 5;   /* 0.00..1.00 */
	case MXM_FIR_FAM_KEYS:     return -100 + idx * 5;   /* -1.00..0.00 */
	case MXM_FIR_FAM_GAUSS:    return   30 + idx * 5;   /* 0.30..1.50 */
	default:                   return        idx * 5;
	}
}

static int fir_ui_p2_val_x100(int fam, int idx)
{
	(void)fam;
	return 0 + idx * 5;   /* C 0.00..1.00 */
}

/* format an x100 value like -0.45 into buf (needs 7 bytes); ranges are -1..+1.5 */
static void fir_ui_fmt_x100(int v100, char *buf)
{
	int a = v100 < 0 ? -v100 : v100;
	int whole = a / 100;
	int frac = a % 100;
	char *p = buf;

	if (v100 < 0)
		*p++ = '-';
	*p++ = (char)('0' + whole);
	*p++ = '.';
	*p++ = (char)('0' + frac / 10);
	*p++ = (char)('0' + frac % 10);
	*p = '\0';
}

/* clamp a step index into a family's valid p1/p2 range */
static int fir_ui_clamp_p1(int fam, int idx)
{
	int mx = fir_ui_p1_max(fam);
	if (idx < 0) return 0;
	return idx > mx ? mx : idx;
}

static int fir_ui_clamp_p2(int fam, int idx)
{
	int mx = fir_ui_p2_max(fam);
	if (idx < 0) return 0;
	return idx > mx ? mx : idx;
}

#endif /* FIR_UI_H */
