/*
 * Copyright © 2012 Intel Corporation
 * Copyright © 2015,2019 Collabora, Ltd.
 * Copyright © 2016 NVIDIA Corporation
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

#ifndef WESTON_DRM_FOURCC_H
#define WESTON_DRM_FOURCC_H

#ifdef __ANDROID__
#include <drm/drm_fourcc.h>
#else
#include <drm_fourcc.h>
#endif

/* The kernel header drm_fourcc.h defines the DRM formats below.  We duplicate
 * some of the definitions here so that building Weston won't require
 * bleeding-edge kernel headers.
 */
#ifndef DRM_FORMAT_XYUV8888
#define DRM_FORMAT_XYUV8888      fourcc_code('X', 'Y', 'U', 'V') /* [31:0] X:Y:Cb:Cr 8:8:8:8 little endian */
#endif

#ifndef DRM_FORMAT_XBGR16161616
#define DRM_FORMAT_XBGR16161616  fourcc_code('X', 'B', '4', '8') /* [63:0] x:B:G:R 16:16:16:16 little endian */
#endif

#ifndef DRM_FORMAT_ABGR16161616
#define DRM_FORMAT_ABGR16161616  fourcc_code('A', 'B', '4', '8') /* [63:0] A:B:G:R 16:16:16:16 little endian */
#endif

#endif
