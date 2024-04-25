/*
 * Copyright © 2013 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _IMAGE_LOADER_H
#define _IMAGE_LOADER_H

#include <pixman.h>

enum weston_image_load_flags {
        WESTON_IMAGE_LOAD_IMAGE = 0x1,
        WESTON_IMAGE_LOAD_ICC = 0x2,
};

struct icc_profile_data {
        int fd;
        uint32_t length;
        uint32_t offset;
};

struct weston_image {
        pixman_image_t *pixman_image;
        struct icc_profile_data *icc_profile_data;
};

struct weston_image *
weston_image_load(const char *filename, uint32_t image_load_flags);

void
weston_image_destroy(struct weston_image *image);

#endif
