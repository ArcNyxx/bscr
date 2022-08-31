/* bscr - screenshot utility
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
#include <xcb/xkb.h>
#include <xcb/xfixes.h>
#include <xcb/xinerama.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-x11.h>

xcb_connection_t *conn;
xcb_screen_t *scr;
FILE *fp = NULL; bool device;

static void blend(uint32_t *dest, uint32_t source);
static void cursor(uint32_t *img, int16_t x, int16_t y, int16_t w, int16_t h);
static void die(const char *fmt, ...);
static void error(png_structp png, png_const_charp msg);
static void mon(int16_t *x, int16_t *y, int16_t *w, int16_t *h, bool query);
static bool sel(int16_t *x, int16_t *y, int16_t *w, int16_t *h);
static void win(int16_t *x, int16_t *y, int16_t *w, int16_t *h, bool query);

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
cursor(uint32_t *img, int16_t x, int16_t y, int16_t w, int16_t h)
{
	xcb_xfixes_query_version_reply_t *ver;
	if ((ver = xcb_xfixes_query_version_reply(conn,
			xcb_xfixes_query_version(conn, ~0, ~0), NULL)) == NULL)
		die("bscr: unable to use xfixes\n");
	free(ver);

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
	die("bscr: unable to write png data: %s\n", msg);
}

static void
mon(int16_t *x, int16_t *y, int16_t *w, int16_t *h, bool query)
{
	xcb_xinerama_query_version_reply_t *ver;
	if ((ver = xcb_xinerama_query_version_reply(conn,
			xcb_xinerama_query_version(conn, ~0, ~0),
			NULL)) == NULL)
		die("bscr: unable to use xinerama\n");
	free(ver);

	xcb_xinerama_query_screens_reply_t *info;
	if ((info = xcb_xinerama_query_screens_reply(conn,
			xcb_xinerama_query_screens(conn), NULL)) == NULL)
		return;
	if (query) {
		xcb_query_pointer_reply_t *ptr;
		if ((ptr = xcb_query_pointer_reply(conn,
				xcb_query_pointer(conn, scr->root),
				NULL)) == NULL)
			die("bscr: unable to query pointer\n");
		*x = ptr->root_x; *y = ptr->root_y;
		free(ptr);
	}

	xcb_xinerama_screen_info_iterator_t iter =
			xcb_xinerama_query_screens_screen_info_iterator(info);
	while (iter.rem > 0) {
		if (*x >= iter.data->x_org && *y >= iter.data->y_org &&
				*x <= iter.data->x_org + iter.data->width &&
				*y <= iter.data->y_org + iter.data->height) {
			*x = iter.data->x_org; *y = iter.data->y_org;
			*w = iter.data->width; *h = iter.data->height;
			break;
		}
		xcb_xinerama_screen_info_next(&iter);
	}
	free(info);

	if (*w == 0 || *h == 0)
		*w = scr->width_in_pixels, *h = scr->height_in_pixels;
}

static bool
sel(int16_t *x, int16_t *y, int16_t *w, int16_t *h)
{
	xcb_window_t win = xcb_generate_id(conn);
	xcb_create_window(conn, scr->root_depth, win, scr->root, 0, 0,
			scr->width_in_pixels, scr->height_in_pixels, 0,
			XCB_WINDOW_CLASS_INPUT_OUTPUT, scr->root_visual,
			XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT,
			(uint32_t []){ scr->white_pixel, true });

	xcb_font_t font = xcb_generate_id(conn);
	xcb_gcontext_t gc = xcb_generate_id(conn);
	xcb_open_font(conn, font, 6, "cursor");
	xcb_create_gc(conn, gc, win, XCB_GC_FOREGROUND |
			XCB_GC_BACKGROUND | XCB_GC_FONT, (uint32_t []){
			scr->white_pixel, scr->black_pixel, font });

	xcb_cursor_t cur[5];
	uint16_t names[5] = { 144, 148, 76, 78, 30 };
	for (int i = 0; i < 5; ++i) {
		cur[i] = xcb_generate_id(conn);
		xcb_create_glyph_cursor(conn, cur[i], font, font,
				names[i], names[i] + 1, 0, 0, 0, ~0, ~0, ~0);
	}
	xcb_close_font(conn, font);

	xcb_xkb_use_extension_cookie_t cver =
			xcb_xkb_use_extension(conn, ~0, ~0);
	xcb_intern_atom_cookie_t ctype, cdock;
	ctype = xcb_intern_atom(conn, false, 19, "_NET_WM_WINDOW_TYPE");
	cdock = xcb_intern_atom(conn, false, 24, "_NET_WM_WINDOW_TYPE_DOCK");
#define MASK XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | \
	XCB_EVENT_MASK_BUTTON_MOTION
	xcb_grab_pointer_cookie_t cptr = xcb_grab_pointer(conn, false, scr->
			root, MASK, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
			XCB_NONE, cur[4], XCB_CURRENT_TIME);
	xcb_grab_keyboard_cookie_t ckey = xcb_grab_keyboard(conn, false,
			scr->root, XCB_CURRENT_TIME,
			XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);

	xcb_xkb_use_extension_reply_t *ver;
	if ((ver = xcb_xkb_use_extension_reply(conn, cver, NULL)) == NULL)
		die("bscr: unable to use xkb\n");
	if (xkb_x11_setup_xkb_extension(conn, XKB_X11_MIN_MAJOR_XKB_VERSION,
			XKB_X11_MIN_MINOR_XKB_VERSION,
			XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS,
			NULL, NULL, NULL, NULL) == 0)
		die("bscr: unable to set up xkb\n");
	struct xkb_context *ctx;
	if ((ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS)) == NULL)
		die("bscr: unable to get xkb context\n");
	int32_t keydev;
	if ((keydev = xkb_x11_get_core_keyboard_device_id(conn)) == -1)
		die("bscr: unable to get keyboard device\n");
	struct xkb_keymap *map;
	if ((map = xkb_x11_keymap_new_from_device(ctx, conn, keydev,
			XKB_KEYMAP_COMPILE_NO_FLAGS)) == NULL)
		die("bscr: unable to make keymap\n");
	struct xkb_state *state;
	if ((state = xkb_x11_state_new_from_device(map, conn, keydev)) == NULL)
		die("bscr: unable to make state\n");

	xcb_intern_atom_reply_t *type, *dock;
	if ((type = xcb_intern_atom_reply(conn, ctype, NULL)) == NULL)
		die("bscr: unable to get atom\n");
	if ((dock = xcb_intern_atom_reply(conn, cdock, NULL)) == NULL)
		die("bscr: unable to get atom\n");
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, type->atom,
			XCB_ATOM_ATOM, 32, 1, &dock->atom);
	xcb_grab_pointer_reply_t *ptr;
	if ((ptr = xcb_grab_pointer_reply(conn, cptr, NULL)) == NULL)
		die("bscr: unable to grab pointer\n");
	xcb_grab_keyboard_reply_t *key;
	if ((key = xcb_grab_keyboard_reply(conn, ckey, NULL)) == NULL)
		die("bscr: unable to grab keyboard\n");
	free(ver); free(type); free(dock); free(ptr); free(key);

	xcb_generic_event_t *evt; bool left = true, press = false;
	while ((evt = xcb_wait_for_event(conn)) != NULL &&
			(evt->response_type & ~0x80) != XCB_BUTTON_RELEASE) {
		switch (evt->response_type & ~0x80) {
		default: break;
		case XCB_BUTTON_PRESS: {
			xcb_button_press_event_t *ev = (void *)evt;
			*x = ev->root_x; *y = ev->root_y;
			left = ev->detail == 1; press = true;
			break;
		}
		case XCB_KEY_PRESS: {
			xcb_key_press_event_t *ev = (void *)evt;
			switch (xkb_state_key_get_one_sym(state, ev->detail)) {
			case XKB_KEY_Right:
				if (*x != scr->width_in_pixels) ++*x;
				break;
			case XKB_KEY_Left:
				if (*x != 0) --*x;
				break;
			case XKB_KEY_Down:
				if (*y != scr->height_in_pixels) ++*y;
				break;
			case XKB_KEY_Up:
				if (*y != 0) --*y;
				break;
			default:
				die("bscr: key pressed\n");
			}
			if (!press)
				break;
		}
			/* FALLTHROUGH */
		case XCB_MOTION_NOTIFY: {
			xcb_motion_notify_event_t *ev =
					(xcb_motion_notify_event_t *)evt;
			xcb_change_active_pointer_grab(conn, cur[(*x <
					ev->root_x) + (*y < ev->root_y) * 2],
					XCB_CURRENT_TIME, MASK);

			*w = ev->root_x - *x, *h = ev->root_y - *y;
			int16_t sx = *x, sy = *y, sw = *w, sh = *h;
			if (sw < 0) sx += sw, sw = -sw;
			if (sh < 0) sy += sh, sh = -sh;
			xcb_shape_rectangles(conn, XCB_SHAPE_SO_SET,
					XCB_SHAPE_SK_BOUNDING, 0, win, 0, 0, 4,
					(xcb_rectangle_t []){
				{ sx, sy, 1, sh }, { sx + sw, sy, 1, sh },
				{ sx, sy, sw, 1 }, { sx, sy + sh, sw, 1 }
			});
			xcb_map_window(conn, win);
			xcb_flush(conn);
		}
		}
		free(evt);
	}
	free(evt);

	xcb_ungrab_pointer(conn, XCB_CURRENT_TIME);
	xcb_ungrab_keyboard(conn, XCB_CURRENT_TIME);
	xcb_unmap_window(conn, win);
	xcb_flush(conn);

	for (int i = 0; i < 5; ++i)
		xcb_free_cursor(conn, cur[i]);
	xcb_free_gc(conn, gc);
	xcb_destroy_window(conn, win);

	if (*w < 0) *x += *w, *w = -*w;
	if (*h < 0) *y += *h, *h = -*h;
	nanosleep(&(struct timespec){ .tv_nsec = 2E8 }, NULL);
	return left;
}

static void
win(int16_t *x, int16_t *y, int16_t *w, int16_t *h, bool query)
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
				die("bscr: unable to query pointer\n");
			if (ptr->child == 0)
				break;
			win = ptr->child;
			free(ptr);
		}
		free(ptr);
	}

	xcb_get_geometry_reply_t *geom;
	if ((geom = xcb_get_geometry_reply(conn,
			xcb_get_geometry(conn, win), NULL)) == NULL)
		die("bscr: unable to get geometry\n");
	*x = geom->x, *y = geom->y;
	*w = geom->width  + 2 * geom->border_width;
	*h = geom->height + 2 * geom->border_width;
	free(geom);
}

int
main(int argc, char **argv)
{
	char opt = 's', *coords = NULL; bool showcur = false;
	if (*(argv = &argv[1]) != NULL && (*argv)[0] == '-') {
		bool end = false; char *optstr = argv[0];
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
				die("bscr: invalid option: -%c\n", optstr[i]);
			}
	}

	int scrnum;
	conn = xcb_connect(NULL, &scrnum);
	const xcb_setup_t *setup = xcb_get_setup(conn);
	xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
	for (int i = 0; i < scrnum; ++i)
		xcb_screen_next(&iter);
	scr = iter.data;

	int16_t x = 0, y = 0, w = 0, h = 0;
	if (opt == 'i') {
		if (coords == NULL)
			die("bscr: must supply option with -i argument\n");
		int16_t arr[3];
		char *start = coords, *end;
		for (int i = 0; i < 3; ++i, start = ++end) {
			arr[i] = strtoul(start, &end, 10);
			if (*end != ',' || !isdigit(*start))
				die("bscr: invalid option: %s\n", coords);
		}
		h = strtoul(start, &end, 10);
		if (*end != '\0' || !isdigit(*start))
			die("bscr: invalid option: %s\n", coords);
		x = arr[0], y = arr[1], w = arr[2];
	} else if (opt == 'm') {
		mon(&x, &y, &w, &h, true);
	} else if (opt == 's') {
		bool left = sel(&x, &y, &w, &h);
		if (w != 0 && h != 0)
			++w, ++h;
		else if (left)
			win(&x, &y, &w, &h, false);
		else
			mon(&x, &y, &w, &h, false);
	} else if (opt == 'w') {
		win(&x, &y, &w, &h, true);
	} else {
		w = scr->width_in_pixels; h = scr->height_in_pixels;
	}

	xcb_get_image_reply_t *res;
	if ((res = xcb_get_image_reply(conn, xcb_get_image(conn,
			XCB_IMAGE_FORMAT_Z_PIXMAP, scr->root,
			x, y, w, h, ~0), NULL)) == NULL)
		die("bscr: unable to get image\n");
	uint32_t *img = (uint32_t *)xcb_get_image_data(res);

	if (showcur)
		cursor(img, x, y, w, h);

	char *buf1, *buf2;
	ssize_t size1 = 256, size2 = 256;
	if ((buf1 = malloc(size1)) == NULL || (buf2 = malloc(size2)) == NULL)
		die("bscr: unable to allocate memory: ");
	strcpy(buf1, "/dev/stdout");

	for (;;) {
		ssize_t len;
		if ((len = readlink(buf1, buf2, size2 - 1)) == -1) {
			if (errno == EINVAL)
				break; /* EINVAL for non-links, finish */
			die("bscr: unable to read symlink: %s: ", buf1);
		}
		if (len == size2 - 1) {
			if ((buf2 = realloc(buf2, size2 * 2)) == NULL)
				die("bscr: unable to allocate memory: ");
			continue; /* not enough room, try again */
		}

		uintptr_t *tmp1 = (void *)&buf1, *tmp2 = (void *)&buf2;
		*tmp1 ^= *tmp2; *tmp2 ^= *tmp1; *tmp1 ^= *tmp2;
		size1 ^= size2; size2 ^= size1; size1 ^= size2;
		buf1[len] = '\0';
	}

	struct stat statbuf;
	if (stat(buf1, &statbuf) == -1)
		die("bscr: unable to stat file: %s: ", buf1);
	free(buf1); free(buf2);
	if ((device = S_ISCHR(statbuf.st_mode))) {
		if ((fp = popen("xclip -sel clip -t image/png", "w")) == NULL)
			die("bscr: unable to open pipe: ");
	} else if ((fp = fopen("/dev/stdout", "wb")) == NULL) {
		die("bscr: unable to open file: /dev/stdout: ");
	}

	png_structp png; png_infop info;
	if ((png = png_create_write_struct(PNG_LIBPNG_VER_STRING,
			NULL, NULL, NULL)) == NULL)
		die("bscr: unable to allocate memory: ");
	if ((info = png_create_info_struct(png)) == NULL)
		die("bscr: unable to allocate memory: ");
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
