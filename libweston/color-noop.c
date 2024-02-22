/*
 * Copyright 2021 Collabora, Ltd.
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

#include "color.h"
#include "shared/helpers.h"
#include "shared/xalloc.h"
#include "shared/weston-assert.h"

struct cmnoop_color_profile {
	struct weston_color_profile base;
};

struct weston_color_manager_noop {
	struct weston_color_manager base;
	struct cmnoop_color_profile *stock_cprof; /* no real content */
};

static bool
check_output_eotf_mode(struct weston_output *output)
{
	if (output->eotf_mode == WESTON_EOTF_MODE_SDR)
		return true;

	weston_log("Error: color manager no-op does not support EOTF mode %s of output %s.\n",
		   weston_eotf_mode_to_str(output->eotf_mode),
		   output->name);
	return false;
}

static struct weston_color_manager_noop *
to_cmnoop(struct weston_color_manager *cm_base)
{
	return container_of(cm_base, struct weston_color_manager_noop, base);
}

static inline struct cmnoop_color_profile *
to_cmnoop_cprof(struct weston_color_profile *cprof_base)
{
	return container_of(cprof_base, struct cmnoop_color_profile, base);
}

static struct cmnoop_color_profile *
ref_cprof(struct cmnoop_color_profile *cprof)
{
	if (!cprof)
		return NULL;

	weston_color_profile_ref(&cprof->base);
	return cprof;
}

static void
unref_cprof(struct cmnoop_color_profile *cprof)
{
	if (!cprof)
		return;

	weston_color_profile_unref(&cprof->base);
}

static void
cmnoop_color_profile_destroy(struct cmnoop_color_profile *cprof)
{
	free(cprof->base.description);
	free(cprof);
}

static void
cmnoop_destroy_color_profile(struct weston_color_profile *cprof_base)
{
	struct cmnoop_color_profile *cprof = to_cmnoop_cprof(cprof_base);

	cmnoop_color_profile_destroy(cprof);
}

static struct cmnoop_color_profile *
cmnoop_color_profile_create(struct weston_color_manager_noop *cm, char *desc)
{
	struct cmnoop_color_profile *cprof;

	cprof = xzalloc(sizeof *cprof);

	weston_color_profile_init(&cprof->base, &cm->base);
	cprof->base.description = desc;

	return cprof;
}

static struct weston_color_profile *
cmnoop_ref_stock_sRGB_color_profile(struct weston_color_manager *cm_base)
{
	struct weston_color_manager_noop *cm = to_cmnoop(cm_base);
	struct cmnoop_color_profile *cprof;

	cprof = ref_cprof(cm->stock_cprof);

	return &cprof->base;
}

static bool
cmnoop_get_color_profile_from_icc(struct weston_color_manager *cm,
				  const void *icc_data,
				  size_t icc_len,
				  const char *name_part,
				  struct weston_color_profile **cprof_out,
				  char **errmsg)
{
	*errmsg = xstrdup("ICC profiles are unsupported.");
	return false;
}

static bool
cmnoop_get_color_profile_from_params(struct weston_color_manager *cm,
				     const struct weston_color_profile_params *params,
				     const char *name_part,
				     struct weston_color_profile **cprof_out,
				     char **errmsg)
{
	*errmsg = xstrdup("parametric profiles are unsupported.");
	return false;
}

static void
cmnoop_destroy_color_transform(struct weston_color_transform *xform)
{
	/* Never called, as never creates an actual color transform. */
}

static bool
cmnoop_get_surface_color_transform(struct weston_color_manager *cm_base,
				   struct weston_surface *surface,
				   struct weston_output *output,
				   struct weston_surface_color_transform *surf_xform)
{
	struct weston_compositor *compositor = output->compositor;
	struct weston_color_manager_noop *cmnoop = to_cmnoop(cm_base);

	/* If surface has a cprof, it has to be the stock one. */
	if (surface->color_profile)
		weston_assert_ptr_eq(compositor, to_cmnoop_cprof(surface->color_profile),
				     cmnoop->stock_cprof);

	/* The output must have a cprof, and it has to be the stock one. */
	weston_assert_ptr(compositor, output->color_profile);
	weston_assert_ptr_eq(compositor, to_cmnoop_cprof(output->color_profile),
			     cmnoop->stock_cprof);

	if (!check_output_eotf_mode(output))
		return false;

	/* Identity transform */
	surf_xform->transform = NULL;
	surf_xform->identity_pipeline = true;

	return true;
}

static struct weston_output_color_outcome *
cmnoop_create_output_color_outcome(struct weston_color_manager *cm_base,
				   struct weston_output *output)
{
	struct weston_compositor *compositor = cm_base->compositor;
	struct weston_color_manager_noop *cmnoop = to_cmnoop(cm_base);
	struct weston_output_color_outcome *co;

	weston_assert_ptr(compositor, output->color_profile);
	weston_assert_ptr_eq(compositor, to_cmnoop_cprof(output->color_profile),
			     cmnoop->stock_cprof);

	if (!check_output_eotf_mode(output))
		return NULL;

	co = xzalloc(sizeof *co);

	/* Identity transform on everything */
	co->from_blend_to_output = NULL;
	co->from_sRGB_to_blend = NULL;
	co->from_sRGB_to_output = NULL;

	co->hdr_meta.group_mask = 0;

	return co;
}

static bool
cmnoop_create_stock_profile(struct weston_color_manager_noop *cm)
{
	char *desc;

	desc = xstrdup("stock sRGB color profile");

	cm->stock_cprof = cmnoop_color_profile_create(cm, desc);
	if (!cm->stock_cprof) {
		free(desc);
		return false;
	}

	return true;
}

static bool
cmnoop_init(struct weston_color_manager *cm_base)
{
	struct weston_color_manager_noop *cm = to_cmnoop(cm_base);

	if (!cmnoop_create_stock_profile(cm))
		return false;

	/* No renderer requirements to check. */
	return true;
}

static void
cmnoop_destroy(struct weston_color_manager *cm_base)
{
	struct weston_color_manager_noop *cmnoop = to_cmnoop(cm_base);

	/* TODO: change this assert to make sure that ref_count is equal to 1.
	 * Currently we have a bug in which we leak surfaces when shutting down
	 * Weston with client surfaces alive, and these surfaces may have a
	 * reference to the stock sRGB profile. */
	weston_assert_uint32_gt_or_eq(cm_base->compositor,
				      cmnoop->stock_cprof->base.ref_count, 1);
	unref_cprof(cmnoop->stock_cprof);

	free(cmnoop);
}

struct weston_color_manager *
weston_color_manager_noop_create(struct weston_compositor *compositor)
{
	struct weston_color_manager_noop *cm;

	cm = xzalloc(sizeof *cm);

	cm->base.name = "no-op";
	cm->base.compositor = compositor;
	cm->base.supports_client_protocol = false;
	cm->base.init = cmnoop_init;
	cm->base.destroy = cmnoop_destroy;
	cm->base.destroy_color_profile = cmnoop_destroy_color_profile;
	cm->base.ref_stock_sRGB_color_profile = cmnoop_ref_stock_sRGB_color_profile;
	cm->base.get_color_profile_from_icc = cmnoop_get_color_profile_from_icc;
	cm->base.get_color_profile_from_params = cmnoop_get_color_profile_from_params;
	cm->base.send_image_desc_info = NULL;
	cm->base.destroy_color_transform = cmnoop_destroy_color_transform;
	cm->base.get_surface_color_transform = cmnoop_get_surface_color_transform;
	cm->base.create_output_color_outcome = cmnoop_create_output_color_outcome;

	/* We don't support anything related to the CM&HDR protocol extension */
	cm->base.supported_color_features = 0;
	cm->base.supported_rendering_intents = 0;
	cm->base.supported_primaries_named = 0;
	cm->base.supported_tf_named = 0;

	return &cm->base;
}
