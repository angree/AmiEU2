/* eui.h - EUI1 images (docs/DATA_FORMAT.md) -> pixels for the screen. Portable C. */
#ifndef EU2_EUI_H
#define EU2_EUI_H

typedef struct {
	int w, h;
	int format;                /* 1 = PAL8, 2 = RGB565 */
	int ncolors;
	unsigned char pal[256 * 3];
	unsigned char *pix;        /* PAL8: w*h bytes; RGB565: w*h big-endian words (2*w*h bytes) */
} EuiImage;

int  eui_decode(const unsigned char *data, unsigned long size, EuiImage *img);   /* 0 = ok */
void eui_free(EuiImage *img);

/* Screen palette mapping. inv is a 4096-byte table RGB444 -> nearest screen pen, built once per
 * screen palette (4096 x npens integer distances; ~1 M multiply-adds). */
#define EUI_INV_SIZE 4096
void eui_build_inverse(const unsigned char *pal, int npens, unsigned char *inv);
/* converts any EUI image to 8-bit screen pens through inv; out is w*h bytes */
void eui_to_pens(const EuiImage *img, const unsigned char *inv, unsigned char *out);
#endif
