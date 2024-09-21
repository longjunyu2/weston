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
#include "config.h"

#include <libweston/libweston.h>
#include "weston.h"

WL_EXPORT void
screenshooter_create(struct weston_compositor *ec)
{

}

WL_EXPORT struct text_backend *
text_backend_init(struct weston_compositor *ec)
{
    return NULL;
}

WL_EXPORT void
text_backend_destroy(struct text_backend *text_backend)
{

}

WL_EXPORT struct weston_config *
wet_get_config(struct weston_compositor *ec)
{
    return NULL;
}

WL_EXPORT char *
wet_get_libexec_path(const char *name)
{
    return NULL;
}

WL_EXPORT struct wl_client *
wet_client_start(struct weston_compositor *compositor, const char *path)
{
    return NULL;
}
