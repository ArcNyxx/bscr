/* lynx - screenshot utility
 * Copyright (C) 2022 ArcNyxx
 * see LICENCE file for licensing information */

#include <stdio.h>

#include <png.h>
#include <xcb/xcb.h>

int
main(int argc, char **argv)
{
	int num;
	xcb_connection_t *conn = xcb_connect(NULL, &num);

	const xcb_setup_t *setup = xcb_get_setup(conn);
	xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
	for (int i = 0; i < num; ++i)
		xcb_screen_next(&iter);
	xcb_screen_t *scr = iter.data;

	xcb_get_image_cookie_t cookie = xcb_get_image(conn,
			XCB_IMAGE_FORMAT_Z_PIXMAP, scr->root, 0, 0,
			scr->width_in_pixels, scr->height_in_pixels, ~0);
	xcb_get_image_reply_t *reply = xcb_get_image_reply(conn, cookie, NULL);
	uint8_t *image = xcb_get_image_data(reply);

	FILE *out = fopen("tmp.png", "wb");
	png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING,
			NULL, NULL, NULL);
	png_infop info_ptr = png_create_info_struct(png_ptr);
	png_init_io(png_ptr, out);
	png_set_IHDR(png_ptr, info_ptr, scr->width_in_pixels,
			scr->height_in_pixels, 8, PNG_COLOR_TYPE_RGB_ALPHA,
			PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
			PNG_FILTER_TYPE_DEFAULT);

	png_set_bgr(png_ptr);

	png_write_info(png_ptr, info_ptr);
	for (int i = 0; i < scr->height_in_pixels; ++i) {
		unsigned char *tmp = &image[i * (4 * scr->width_in_pixels)];
		png_write_rows(png_ptr, &tmp, 1);
	}
	png_write_end(png_ptr, NULL);
	fclose(out);
}
