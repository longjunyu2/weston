/*
 * Copyright 2024 Junyu Long
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

#ifndef WESTON_COMPOSITOR_ANDROID_H
#define WESTON_COMPOSITOR_ANDROID_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <libweston/libweston.h>

#define WESTON_ANDROID_BACKEND_CONFIG_VERSION 1

struct weston_android_backend_config {
    struct weston_backend_config base;

    /** Select the renderer to use */
    enum weston_renderer_type renderer;

    /** Output repaint refresh rate (in mHz). Supported values range from 0
     * mHz to 1,000,000 mHz. 0 is a special value that triggers repaints
     * only on capture requests, not on damages. */
    int refresh;

    void* jni;
};

#ifdef  __cplusplus
}
#endif

#endif /* WESTON_COMPOSITOR_ANDROID_H */
