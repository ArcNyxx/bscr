/* lynx - screenshot utility
 * Copyright (C) 2022 ArcNyxx
 * see LICENCE file for licensing information */

#include <ctype.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <png.h>
#include <xcb/xcb.h>

xcb_connection_t *conn;

static void die(const char *fmt, ...);

static void
die(const char *fmt, ...)
{
	va_list list;
	va_start(list, fmt);
	vfprintf(stderr, fmt, list);
	va_end(list);

	if (fmt[strlen(fmt) - 1] != '\n')
		perror(NULL);
	if (conn != NULL)
		xcb_disconnect(conn);
	exit(1);
}

int
main(int argc, char **argv)
{
	short x = 0, y = 0, w = 0, h = 0, opt = 2;
	char *coords = NULL;
	bool showcur = false, freeze = false;

	if (*(argv = &argv[1]) != NULL && (*argv)[0] == '-') {
		bool end = false;
		char *optstr = *argv;
		for (int i = 1; optstr[i] != '\0'; ++i)
				switch (optstr[i]) {
		case 'c': showcur = !showcur; break;
		case 'f': freeze  = !freeze;  break;

		case 'a': opt = 0; break;
		case 'i': opt = 1;
			if (!end && (coords = *(argv = &argv[1])) == NULL)
				end = true;
			break;
		case 's': opt = 2; break;
		case 'm': opt = 3; break;
		case 'w': opt = 4; break;

		default:
			die("lynx: invalid option: -%c\n", optstr[i]);
		}
	}

	int n;
	conn = xcb_connect(NULL, &n);
	const xcb_setup_t *setup = xcb_get_setup(conn);
	xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
	for (int i = 0; i < n; ++i)
		xcb_screen_next(&iter);
	xcb_screen_t *scr = iter.data;

	if (opt == 1) {
		char *start = coords, *end;
		for (int i = 0; i < 3; ++i, start = ++end) {
			((short *)&x)[i] = strtoul(start, &end, 10);
			if (*end != ',' || !isdigit(*start))
				die("lynx: invalid option: %s\n", coords);
		}
		h = strtoul(start, &end, 10);
		if (*end != '\0' || !isdigit(*start))
			die("lynx: invalid option: %s\n", coords);
	} else {
		w = scr->width_in_pixels; h = scr->height_in_pixels;
	}

	xcb_get_image_cookie_t cimg = xcb_get_image(conn,
			XCB_IMAGE_FORMAT_Z_PIXMAP, scr->root, x, y, w, h, ~0);
	xcb_get_image_reply_t *rimg = xcb_get_image_reply(conn, cimg, NULL);
	uint8_t *img = xcb_get_image_data(rimg);

	FILE *file;
	if ((file = fopen("/dev/stdout", "wb")) == NULL)
		die("lynx: unable to open file: %s\n", "/dev/stdout");
	png_structp png;
	if ((png = png_create_write_struct(PNG_LIBPNG_VER_STRING,
			NULL, NULL, NULL)) == NULL)
		die("lynx: unable to allocate memory\n");
	png_infop info;
	if ((info = png_create_info_struct(png)) == NULL)
		die("lynx: unable to allocate memory\n");
	png_init_io(png, file);
	png_set_IHDR(png, info, w, h, 8, PNG_COLOR_TYPE_RGB_ALPHA,
			PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
			PNG_FILTER_TYPE_DEFAULT);
	png_set_bgr(png);

	if (setjmp(png_jmpbuf(png))) {
		png_destroy_write_struct(&png, &info);
		fclose(file);
		return 1;
	}

	png_write_info(png, info);
	for (int i = 0; i < h; ++i)
		png_write_rows(png, &(unsigned char *){ &img[i * w * 4] }, 1);
	png_write_end(png, NULL);
	png_destroy_write_struct(&png, &info);
	fclose(file);

	xcb_disconnect(conn);
}
