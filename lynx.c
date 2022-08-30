/* lynx - screenshot utility
 * Copyright (C) 2022 ArcNyxx
 * see LICENCE file for licensing information */

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <png.h>
#include <xcb/xcb.h>
#include <xcb/xfixes.h>
#include <xcb/xinerama.h>

xcb_connection_t *conn;
xcb_screen_t *scr;
FILE *fp = NULL; bool device;

static void blend(uint32_t *dest, uint32_t source);
static void cursor(uint32_t *img, short x, short y, short w, short h);
static void die(const char *fmt, ...);
static void error(png_structp png, png_const_charp msg);
static void monitor(short *x, short *y, short *w, short *h, bool query);
static void sel(short *x, short *y, short *w, short *h);
static void window(short *x, short *y, short *w, short *h, bool query);

static void
blend(uint32_t *dest, uint32_t source)
{
	float alpha = (float)(source >> 24 & 255) / 255.0;
	uint32_t diff1 = (*dest >>  0 & 255) * (1.0 - alpha) +
		(source >>  0 & 255) * alpha;
	uint32_t diff2 = (*dest >>  8 & 255) * (1.0 - alpha) +
		(source >>  8 & 255) * alpha;
	uint32_t diff3 = (*dest >> 16 & 255) * (1.0 - alpha) +
		(source >> 16 & 255) * alpha;
	*dest = 255 << 24 | diff3 << 16 | diff2 << 8 | diff1;
}

static void
cursor(uint32_t *img, short x, short y, short w, short h)
{
	xcb_xfixes_query_version_reply_t *qver;
	if ((qver = xcb_xfixes_query_version_reply(conn,
			xcb_xfixes_query_version(conn, ~0, ~0), NULL)) == NULL)
		die("lynx: unable to use xfixes\n");
	free(qver);

	xcb_xfixes_get_cursor_image_reply_t *res =
			xcb_xfixes_get_cursor_image_reply(conn,
			xcb_xfixes_get_cursor_image(conn), NULL);
	uint32_t *cur = xcb_xfixes_get_cursor_image_cursor_image(res);
	for (int i = 0; i < res->height; ++i) {
		for (int j = 0; j < res->width; ++j) {
			if (res->x + j >= x && res->y + i >= y && 
					res->x - res->xhot - x + j < w &&
					res->y - res->yhot - y + i < h)
				blend(&img[
					(res->y - res->yhot + i - y) * w +
					(res->x - res->xhot + j - x)
				], cur[i * res->width + j]);
		}
	}
	free(res);
}

static void
die(const char *fmt, ...)
{
	va_list list;
	va_start(list, fmt);
	vfprintf(stderr, fmt, list);
	va_end(list);

	if (fmt[strlen(fmt) - 1] != '\n')
		perror(NULL);
	if (fp != NULL)
		!device ? fclose(fp) : pclose(fp);
	if (conn != NULL)
		xcb_disconnect(conn);
	exit(1);
}

static void
error(png_structp png, png_const_charp msg)
{
	die("lynx: unable to write png data: %s\n", msg);
}

static void
monitor(short *x, short *y, short *w, short *h, bool query)
{
	xcb_xinerama_query_version_reply_t *qver;
	if ((qver = xcb_xinerama_query_version_reply(conn,
			xcb_xinerama_query_version(conn, ~0, ~0),
			NULL)) == NULL)
		die("lynx: unable to use xinerama\n");
	free(qver);

	xcb_xinerama_query_screens_reply_t *info;
	if ((info = xcb_xinerama_query_screens_reply(conn,
			xcb_xinerama_query_screens(conn), NULL)) == NULL) {
		free(info);
		return;
	}
	xcb_xinerama_screen_info_iterator_t iter =
			xcb_xinerama_query_screens_screen_info_iterator(info);
	if (query) {
		xcb_query_pointer_reply_t *ptr = xcb_query_pointer_reply(conn,
				xcb_query_pointer(conn, scr->root), NULL);
		*x = ptr->root_x; *y = ptr->root_y;
		free(ptr);
	}

	while (iter.rem > 0) {
		xcb_xinerama_screen_info_next(&iter);
		const xcb_xinerama_screen_info_t *data = iter.data;
		if (*x >= data->x_org && *y >= data->y_org &&
				*x <= data->x_org + data->width &&
				*y <= data->y_org + data->height) {
			*x = data->x_org; *y = data->y_org;
			*w = data->width; *h = data->height;
			break;
		}
	}
	free(info);
}

static void
sel(short *x, short *y, short *w, short *h)
{
#define WINMASK XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK | XCB_CW_BACK_PIXEL
#define EVTMASK XCB_EVENT_MASK_EXPOSURE
#define MASK XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | \
	XCB_EVENT_MASK_BUTTON_MOTION

	xcb_window_t win = xcb_generate_id(conn);
	xcb_create_window(conn, scr->root_depth, win, scr->root, 0, 0,
			*w, *h, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
			scr->root_visual, WINMASK,
			(uint32_t []){ scr->white_pixel, true, EVTMASK | MASK });

	xcb_intern_atom_cookie_t ctatom, cdatom;
	xcb_intern_atom_reply_t *rtatom, *rdatom;
	ctatom = xcb_intern_atom(conn, false, 19, "_NET_WM_WINDOW_TYPE");
	cdatom = xcb_intern_atom(conn, false, 24, "_NET_WM_WINDOW_TYPE_DOCK");
	if ((rtatom = xcb_intern_atom_reply(conn, ctatom, NULL)) == NULL)
		die("lynx: unable to get atom\n");
	if ((rdatom = xcb_intern_atom_reply(conn, cdatom, NULL)) == NULL)
		die("lynx: unable to get atom\n");
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, rtatom->atom,
			XCB_ATOM_ATOM, 32, 1, (void *){ &rdatom->atom });


	xcb_grab_pointer_cookie_t cptr = xcb_grab_pointer(conn, false,
			scr->root, MASK, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
			XCB_NONE, XCB_NONE, XCB_CURRENT_TIME);
	xcb_grab_keyboard_cookie_t ckey = xcb_grab_keyboard(conn,
			false, scr->root, XCB_CURRENT_TIME,
			XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
	xcb_grab_pointer_reply_t *ptr;
	if ((ptr = xcb_grab_pointer_reply(conn, cptr, NULL)) == NULL)
		die("lynx: unable to grab pointer\n");
	xcb_grab_keyboard_reply_t *key;
	if ((key = xcb_grab_keyboard_reply(conn, ckey, NULL)) == NULL)
		die("lynx: unable to grab keyboard\n");
	free(ptr); free(key);


	xcb_generic_event_t *evt;
	while ((evt = xcb_wait_for_event(conn)) != NULL &&
			(evt->response_type & ~0x80) != XCB_BUTTON_RELEASE) {
		switch (evt->response_type & ~0x80) {
		case XCB_BUTTON_PRESS: {
			xcb_button_press_event_t *ev =
					(xcb_button_press_event_t *)evt;
			*x = ev->root_x, *y = ev->root_y;
			break;
		}
		case XCB_MOTION_NOTIFY: ;
			xcb_motion_notify_event_t *ev =
					(xcb_motion_notify_event_t *)evt;
			*w = ev->root_x - *x, *h = ev->root_y - *y;
			/* FALLTHROUGH */

			int16_t sx = *x, sy = *y, sw = *w, sh = *h;
			if (sw < 0) sx += sw, sw = -sw;
			if (sh < 0) sy += sh, sh = -sh;
			//const xcb_point_t pts[5] = { { sx, sy }, { sw, 0 },
					//{ 0, sh }, { -sw, 0 }, { 0, -sh } };
			//xcb_poly_line(d, XCB_COORD_MODE_PREVIOUS, win, gc, 5,
					//pts);

	const xcb_rectangle_t arr[4] = { { sx, sy, 1, sh }, { sx + sw, sy, 1, sh },
                                { sx, sy, sw, 1 }, { sx, sy + sh, sw, 1 } };
	xcb_shape_rectangles(conn, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_BOUNDING,
			0, win, 0, 0, 4, arr);
	xcb_map_window(conn, win);







			/* FALLTHROUGH */
		case XCB_EXPOSE: ;
			xcb_flush(conn);
			break;
		default: break;
		}
		free(evt);
	}
	free(evt);

	xcb_unmap_window(conn, win);

	if (*w < 0) *x += *w, *w = -*w;
	++*w;
	if (*h < 0) *y += *h, *h = -*h;
	++*h;
}

static void
window(short *x, short *y, short *w, short *h, bool query)
{
	xcb_window_t win = scr->root;
	if (query) {
		xcb_get_input_focus_reply_t *focus;
		if ((focus = xcb_get_input_focus_reply(conn,
				xcb_get_input_focus(conn), NULL)) != NULL) {
			win = focus->focus;
			free(focus);
		}
	} else {
		xcb_query_pointer_reply_t *ptr;
		for (;;) {
			if ((ptr = xcb_query_pointer_reply(conn,
					xcb_query_pointer(conn, win),
					NULL)) == NULL)
				die("lynx: unable to query pointer\n");
			if (ptr->same_screen)
				break;
			free(ptr);
		}
		free(ptr);
	}

	xcb_get_geometry_reply_t *geom;
	if ((geom = xcb_get_geometry_reply(conn,
			xcb_get_geometry(conn, win), NULL)) == NULL)
		die("lynx: unable to get geometry\n");
	*x = geom->x, *y = geom->y;
	*w = geom->width  + 2 * geom->border_width;
	*h = geom->height + 2 * geom->border_width;
	free(geom);
}

int
main(int argc, char **argv)
{
	char opt = 's', *coords = NULL;
	bool showcur = false;
	if (*(argv = &argv[1]) != NULL && (*argv)[0] == '-') {
		bool end = false; /* ensure defined behaviour NULL bound */
		char *optstr = *argv;
		for (int i = 1; optstr[i] != '\0'; ++i)
			switch (optstr[i]) {
			case 'c':
				showcur = true;
				break;
			case 'i':
				if (!end && (coords =
						*(argv = &argv[1])) == NULL)
					  end = true;
				/* FALLTHROUGH */
			case 'a':
			case 'm':
			case 's':
			case 'w':
				opt = optstr[i];
				break;
			default:
				die("lynx: invalid option: -%c\n", optstr[i]);
			}
	}

	int screen;
	conn = xcb_connect(NULL, &screen);
	const xcb_setup_t *setup = xcb_get_setup(conn);
	xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
	for (int i = 0; i < screen; ++i)
		xcb_screen_next(&iter);
	scr = iter.data;

	int16_t x = 0, y = 0,
			w = scr->width_in_pixels, h = scr->height_in_pixels;
	if (opt == 'i') {
		if (coords == NULL)
			die("lynx: must supply option with -i argument\n");

		int16_t arr[3];
		char *start = coords, *end;
		for (int i = 0; i < 3; ++i, start = ++end) {
			arr[i] = strtoul(start, &end, 10);
			if (*end != ',' || !isdigit(*start))
				die("lynx: invalid option: %s\n", coords);
		}
		h = strtoul(start, &end, 10);
		if (*end != '\0' || !isdigit(*start))
			die("lynx: invalid option: %s\n", coords);
		x = arr[0], y = arr[1], w = arr[2];
	} else if (opt == 'm') {
		monitor(&x, &y, &w, &h, true);
	} else if (opt == 's') {
		sel(&x, &y, &w, &h);
	} else if (opt == 'w') {
		window(&x, &y, &w, &h, true);
	}

	xcb_get_image_reply_t *res;
	if ((res = xcb_get_image_reply(conn, xcb_get_image(conn,
			XCB_IMAGE_FORMAT_Z_PIXMAP, scr->root,
			x, y, w, h, ~0), NULL)) == NULL)
		die("lynx: unable to get image\n");
	uint32_t *img = (uint32_t *)xcb_get_image_data(res);

	if (showcur)
		cursor(img, x, y, w, h);

	char *buf1, *buf2;
	ssize_t size1 = 256, size2 = 256;
	if ((buf1 = malloc(size1)) == NULL || (buf2 = malloc(size2)) == NULL)
		die("lynx: unable to allocate memory: ");
	strcpy(buf1, "/dev/stdout");

	for (;;) {
		ssize_t len;
		if ((len = readlink(buf1, buf2, size2 - 1)) == -1) {
			if (errno == EINVAL)
				break; /* EINVAL for non-links, finish */
			die("lynx: unable to read symlink: %s: ", buf1);
		}
		if (len == size2 - 1) {
			if ((buf2 = realloc(buf2, size2 * 2)) == NULL)
				die("lynx: unable to allocate memory: ");
			continue; /* not enough room, try again */
		}

		uintptr_t *tmp1 = (void *)&buf1, *tmp2 = (void *)&buf2;
		*tmp1 ^= *tmp2; *tmp2 ^= *tmp1; *tmp1 ^= *tmp2;
		size1 ^= size2; size2 ^= size1; size1 ^= size2;
		buf1[len] = '\0';
	}

	struct stat statbuf;
	if (stat(buf1, &statbuf) == -1)
		die("lynx: unable to stat file: %s: ", buf1);
	free(buf1); free(buf2);
	if ((device = S_ISCHR(statbuf.st_mode))) {
		if ((fp = popen("xclip -sel clip -t image/png", "w")) == NULL)
			die("lynx: unable to open pipe: ");
	} else if ((fp = fopen("/dev/stdout", "wb")) == NULL) {
		die("lynx: unable to open file: /dev/stdout: ");
	}

	png_structp png; png_infop info;
	if ((png = png_create_write_struct(PNG_LIBPNG_VER_STRING,
			NULL, NULL, NULL)) == NULL)
		die("lynx: unable to allocate memory: ");
	if ((info = png_create_info_struct(png)) == NULL)
		die("lynx: unable to allocate memory: ");
	png_set_error_fn(png, NULL, error, error);
	png_set_IHDR(png, info, w, h, 8, PNG_COLOR_TYPE_RGB_ALPHA,
			PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
			PNG_FILTER_TYPE_DEFAULT);
	png_init_io(png, fp);
	png_set_bgr(png);

	png_write_info(png, info);
	for (int i = 0; i < h; ++i)
		png_write_rows(png, &(png_bytep){ (png_bytep)&img[i * w] }, 1);
	png_write_end(png, NULL);

	free(res);
	png_destroy_write_struct(&png, &info);
	!device ? fclose(fp) : pclose(fp);
	xcb_disconnect(conn);
}
