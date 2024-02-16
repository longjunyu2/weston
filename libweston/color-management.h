/*
 * Copyright 2023 Collabora, Ltd.
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

#ifndef WESTON_COLOR_MANAGEMENT_H
#define WESTON_COLOR_MANAGEMENT_H

#include "color-properties.h"

struct cm_image_desc_info;

int
weston_compositor_enable_color_management_protocol(struct weston_compositor *compositor);

void
weston_output_send_image_description_changed(struct weston_output *output);

void
weston_surface_send_preferred_image_description_changed(struct weston_surface *surface);

void
weston_cm_send_icc_file(struct cm_image_desc_info *cm_image_desc_info,
                        int32_t fd, uint32_t len);

void
weston_cm_send_primaries_named(struct cm_image_desc_info *cm_image_desc_info,
			       const struct weston_color_primaries_info *primaries_info);

void
weston_cm_send_primaries(struct cm_image_desc_info *cm_image_desc_info,
                         const struct weston_color_gamut *color_gamut);

void
weston_cm_send_tf_named(struct cm_image_desc_info *cm_image_desc_info,
                        const struct weston_color_tf_info *tf_info);

#endif /* WESTON_COLOR_MANAGEMENT_H */
