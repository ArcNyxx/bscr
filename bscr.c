/* bscr - screenshot utility
 * Copyright (C) 2022 ArcNyxx
 * see LICENCE file for licensing information */

#include <ctype.h>
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

static void blend(uint32_t *dest, uint32_t source);
static void cursor(uint32_t *img, int x, int y, int w, int h);
static void die(const char *fmt, ...);
static void mon(int *x, int *y, int *w, int *h, bool query);
static bool sel(int *x, int *y, int *w, int *h);
static void win(int *x, int *y, int *w, int *h, bool query);

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
cursor(uint32_t *img, int x, int y, int w, int h)
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
	if (conn != NULL)
		xcb_disconnect(conn);
	exit(1);
}

static void
mon(int *x, int *y, int *w, int *h, bool query)
{
	xcb_xinerama_query_version_reply_t *ver;
	if ((ver = xcb_xinerama_query_version_reply(conn,
			xcb_xinerama_query_version(conn, ~0, ~0),
			NULL)) == NULL)
		die("bscr: unable to use xinerama\n");
	free(ver);

	xcb_xinerama_query_screens_cookie_t inc =
			xcb_xinerama_query_screens(conn);
	if (query) {
		xcb_query_pointer_reply_t *ptr;
		if ((ptr = xcb_query_pointer_reply(conn,
				xcb_query_pointer(conn, scr->root),
				NULL)) == NULL)
			die("bscr: unable to query pointer\n");
		*x = ptr->root_x; *y = ptr->root_y;
		free(ptr);
	}

	xcb_xinerama_query_screens_reply_t *info;
	if ((info = xcb_xinerama_query_screens_reply(conn, inc, NULL)) == NULL)
		return;
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
}

static bool
sel(int *x, int *y, int *w, int *h)
{
	xcb_window_t win = xcb_generate_id(conn);
	if (xcb_request_check(conn, xcb_create_window_checked(conn,
			scr->root_depth, win, scr->root, 0, 0,
			scr->width_in_pixels, scr->height_in_pixels, 0,
			XCB_WINDOW_CLASS_INPUT_OUTPUT, scr->root_visual,
			XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT,
			(uint32_t []){ scr->white_pixel, true })) != NULL)
		die("bscr: unable to create window\n");

	xcb_font_t font = xcb_generate_id(conn);
	if (xcb_request_check(conn, xcb_open_font_checked(conn,
			font, 6, "cursor")) != NULL)
		die("bscr: unable to open cursor font\n");

	xcb_gcontext_t gc = xcb_generate_id(conn);
	if (xcb_request_check(conn, xcb_create_gc_checked(conn, gc, win,
			XCB_GC_FOREGROUND | XCB_GC_BACKGROUND | XCB_GC_FONT,
			(uint32_t []){ scr->white_pixel,
			scr->black_pixel, font })) != NULL)
		die("bscr: unable to create graphics context\n");

	xcb_cursor_t cur[5];
	const unsigned int names[5] = { 144, 148, 76, 78, 30 };
	for (int i = 0; i < 5; ++i) {
		cur[i] = xcb_generate_id(conn);
		if (xcb_request_check(conn,
				xcb_create_glyph_cursor_checked(conn, cur[i],
				font, font, names[i], names[i] + 1,
				0, 0, 0, ~0, ~0, ~0)) != NULL)
			die("bscr: unable to create cursor\n");
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

	*w = *h = 0;
	xcb_generic_event_t *evt;
	bool left = true, press = false;
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
			int sx = *x, sy = *y, sw = *w, sh = *h;
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

	xkb_state_unre(state);
	xkb_keymap_unref(map);
	xkb_context_unref(ctx);
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
win(int *x, int *y, int *w, int *h, bool query)
{
	xcb_window_t win = scr->root;
	if (query) {
		xcb_get_input_focus_reply_t *focus;
		if ((focus = xcb_get_input_focus_reply(conn,
				xcb_get_input_focus(conn), NULL)) != NULL)
			win = focus->focus, free(focus);
	} else for (xcb_query_pointer_reply_t *ptr; ; ) {
		if ((ptr = xcb_query_pointer_reply(conn,
				xcb_query_pointer(conn, win), NULL)) == NULL)
			die("bscr: unable to query pointer\n");
		if (ptr->child != 0) {
			win = ptr->child, free(ptr);
		} else {
			free(ptr); break;
		}
	}

	xcb_get_geometry_reply_t *geom;
	if ((geom = xcb_get_geometry_reply(conn,
			xcb_get_geometry(conn, win), NULL)) == NULL)
		die("bscr: unable to get geometry\n");
	*x = geom->x, *y = geom->y, *w = geom->width + 2 * geom->border_width,
			*h = geom->height + 2 * geom->border_width;
	free(geom);
}

int
main(int argc, char **argv)
{
	bool showcur = false, end = false;
	char opt = 's', *coords = NULL, *optstr = argv[1];
	if (*(argv = &argv[1]) != NULL && argv[0][0] == '-')
		for (int i = 1; optstr[i] != '\0'; ++i)
			switch (optstr[i]) {
			case 'c':
				showcur = true; break;
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

	int scrnum;
	conn = xcb_connect(NULL, &scrnum);
	xcb_screen_iterator_t iter =
			xcb_setup_roots_iterator(xcb_get_setup(conn));
	for (int i = 0; i < scrnum; ++i)
		xcb_screen_next(&iter);
	scr = iter.data;

	int x = 0, y = 0, w = scr->width_in_pixels, h = scr->height_in_pixels;
	if (opt == 'i') {
		if (coords == NULL)
			die("bscr: must supply option with -i argument\n");
		char *start = coords, *last;
		for (int i = 0; i < 3; ++i, start = ++last) {
			((int *)&x)[i] = strtoul(start, &last, 10);
			if (*last != ',' || !isdigit(*start))
				die("bscr: invalid option: %s\n", coords);
		}
		h = strtoul(start, &last, 10);
		if (*last != '\0' || !isdigit(*start))
			die("bscr: invalid option: %s\n", coords);
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
	}

	xcb_get_image_reply_t *res;
	if ((res = xcb_get_image_reply(conn, xcb_get_image(conn,
			XCB_IMAGE_FORMAT_Z_PIXMAP, scr->root,
			x, y, w, h, ~0), NULL)) == NULL)
		die("bscr: unable to get image\n");
	uint32_t *img = (uint32_t *)xcb_get_image_data(res);

	if (showcur)
		cursor(img, x, y, w, h);

	png_structp png; png_infop info;
	if ((png = png_create_write_struct(PNG_LIBPNG_VER_STRING,
			NULL, NULL, NULL)) == NULL)
		die("bscr: unable to allocate memory: ");
	if ((info = png_create_info_struct(png)) == NULL)
		die("bscr: unable to allocate memory: ");
	png_set_IHDR(png, info, w, h, 8, PNG_COLOR_TYPE_RGB_ALPHA,
			PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
			PNG_FILTER_TYPE_DEFAULT);
	png_set_bgr(png);

	FILE *fp;
	if (isatty(STDOUT_FILENO)) {
		if ((fp = popen("graxp -t image/png >/dev/null", "w")) == NULL)
			die("bscr: unable to open pipe: ");
	} else if ((fp = fdopen(STDOUT_FILENO, "wb")) == NULL) {
		die("bscr: unable to open stdout: ");
	}

	png_init_io(png, fp);
	png_write_info(png, info);
	for (int i = 0; i < h; ++i)
		png_write_row(png, (unsigned char *)&img[i * w]);
	png_write_end(png, NULL);

	isatty(STDOUT_FILENO) ? fclose(fp) : pclose(fp);
	png_destroy_write_struct(&png, &info);
	free(res); xcb_disconnect(conn);
}
