/*
 * Copyright 2021 Collabora, Ltd.
 * Copyright 2021 Advanced Micro Devices, Inc.
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

#include <assert.h>
#include <string.h>
#include <libweston/libweston.h>

#include "color.h"
#include "color-lcms.h"
#include "color-properties.h"
#include "shared/helpers.h"
#include "shared/xalloc.h"
#include "shared/weston-assert.h"

const char *
cmlcms_category_name(enum cmlcms_category cat)
{
	static const char *const category_names[] = {
		[CMLCMS_CATEGORY_INPUT_TO_BLEND] = "input-to-blend",
		[CMLCMS_CATEGORY_BLEND_TO_OUTPUT] = "blend-to-output",
		[CMLCMS_CATEGORY_INPUT_TO_OUTPUT] = "input-to-output",
	};

	if (cat < 0 || cat >= ARRAY_LENGTH(category_names))
		return "[illegal category value]";

	return category_names[cat] ?: "[undocumented category value]";
}

static const struct weston_render_intent_info *
render_intent_from_surface_or_default(struct weston_color_manager_lcms *cm,
				      struct weston_surface *surface)
{
	if (surface && surface->render_intent)
		return surface->render_intent;

	/*
	 * When color-management protocol has not been used to set a rendering
	 * intent, we can freely choose our own. Perceptual is the best
	 * considering the use case of color unaware apps on a HDR screen.
	 */
	return weston_render_intent_info_from(cm->base.compositor,
					      WESTON_RENDER_INTENT_PERCEPTUAL);
}

static struct cmlcms_color_profile *
to_cprof_or_stock_sRGB(struct weston_color_manager_lcms *cm,
		       struct weston_color_profile *cprof_base)
{
	if (cprof_base)
		return to_cmlcms_cprof(cprof_base);
	else
		return cm->sRGB_profile;
}

static void
cmlcms_destroy_color_transform(struct weston_color_transform *xform_base)
{
	struct cmlcms_color_transform *xform = to_cmlcms_xform(xform_base);

	cmlcms_color_transform_destroy(xform);
}

static bool
cmlcms_get_surface_color_transform(struct weston_color_manager *cm_base,
				   struct weston_surface *surface,
				   struct weston_output *output,
				   struct weston_surface_color_transform *surf_xform)
{
	struct weston_color_manager_lcms *cm = to_cmlcms(cm_base);
	struct cmlcms_color_transform *xform;
	struct cmlcms_color_transform_search_param param = {
		.category = CMLCMS_CATEGORY_INPUT_TO_BLEND,
		.input_profile = to_cprof_or_stock_sRGB(cm, surface->color_profile),
		.output_profile = to_cprof_or_stock_sRGB(cm, output->color_profile),
		.render_intent = render_intent_from_surface_or_default(cm, surface),
	};

	xform = cmlcms_color_transform_get(cm, &param);
	if (!xform)
		return false;

	surf_xform->transform = &xform->base;
	/*
	 * TODO: Instead of this, we should create the INPUT_TO_OUTPUT color
	 * transformation and check if that is identity. Comparing just the
	 * profiles will miss image adjustments if we add some.
	 * OTOH, that will only be useful if DRM-backend learns to do
	 * opportunistic direct scanout without KMS blending space
	 * transformations.
	 */
	if (xform->search_key.input_profile == xform->search_key.output_profile)
		surf_xform->identity_pipeline = true;
	else
		surf_xform->identity_pipeline = false;

	return true;
}

static bool
cmlcms_get_blend_to_output_color_transform(struct weston_color_manager_lcms *cm,
					   struct weston_output *output,
					   struct weston_color_transform **xform_out)
{
	struct cmlcms_color_transform *xform;
	struct cmlcms_color_transform_search_param param = {
		.category = CMLCMS_CATEGORY_BLEND_TO_OUTPUT,
		.input_profile = NULL,
		.output_profile = to_cprof_or_stock_sRGB(cm, output->color_profile),
		.render_intent = NULL,
	};

	xform = cmlcms_color_transform_get(cm, &param);
	if (!xform)
		return false;

	*xform_out = &xform->base;
	return true;
}

static bool
cmlcms_get_sRGB_to_output_color_transform(struct weston_color_manager_lcms *cm,
					  struct weston_output *output,
					  struct weston_color_transform **xform_out)
{
	struct cmlcms_color_transform *xform;
	struct cmlcms_color_transform_search_param param = {
		.category = CMLCMS_CATEGORY_INPUT_TO_OUTPUT,
		.input_profile = cm->sRGB_profile,
		.output_profile = to_cprof_or_stock_sRGB(cm, output->color_profile),
		.render_intent = render_intent_from_surface_or_default(cm, NULL),
	};

	/*
	 * Create a color transformation when output profile is not stock
	 * sRGB profile.
	 */
	if (param.output_profile != cm->sRGB_profile) {
		xform = cmlcms_color_transform_get(cm, &param);
		if (!xform)
			return false;
		*xform_out = &xform->base;
	} else {
		*xform_out = NULL; /* Identity transform */
	}

	return true;
}

static bool
cmlcms_get_sRGB_to_blend_color_transform(struct weston_color_manager_lcms *cm,
					 struct weston_output *output,
					 struct weston_color_transform **xform_out)
{
	struct cmlcms_color_transform *xform;
	struct cmlcms_color_transform_search_param param = {
		.category = CMLCMS_CATEGORY_INPUT_TO_BLEND,
		.input_profile = cm->sRGB_profile,
		.output_profile = to_cprof_or_stock_sRGB(cm, output->color_profile),
		.render_intent = render_intent_from_surface_or_default(cm, NULL),
	};

	xform = cmlcms_color_transform_get(cm, &param);
	if (!xform)
		return false;

	*xform_out = &xform->base;
	return true;
}

static float
meta_clamp(float value, const char *valname, float min, float max,
	   struct weston_output *output)
{
	float ret = value;

	/* Paranoia against NaN */
	if (!(ret >= min))
		ret = min;

	if (!(ret <= max))
		ret = max;

	if (ret != value) {
		weston_log("output '%s' clamping %s value from %f to %f.\n",
			   output->name, valname, value, ret);
	}

	return ret;
}

static bool
cmlcms_get_hdr_meta(struct weston_output *output,
		    struct weston_hdr_metadata_type1 *hdr_meta)
{
	const struct weston_color_characteristics *cc;

	/* TODO: get color characteristics from color profiles instead. */

	hdr_meta->group_mask = 0;

	/* Only SMPTE ST 2084 mode uses HDR Static Metadata Type 1 */
	if (weston_output_get_eotf_mode(output) != WESTON_EOTF_MODE_ST2084)
		return true;

	cc = weston_output_get_color_characteristics(output);

	/* Target content chromaticity */
	if (cc->group_mask & WESTON_COLOR_CHARACTERISTICS_GROUP_PRIMARIES) {
		unsigned i;

		for (i = 0; i < 3; i++) {
			hdr_meta->primary[i].x = meta_clamp(cc->primary[i].x,
							    "primary", 0.0, 1.0,
							    output);
			hdr_meta->primary[i].y = meta_clamp(cc->primary[i].y,
							    "primary", 0.0, 1.0,
							    output);
		}
		hdr_meta->group_mask |= WESTON_HDR_METADATA_TYPE1_GROUP_PRIMARIES;
	}

	/* Target content white point */
	if (cc->group_mask & WESTON_COLOR_CHARACTERISTICS_GROUP_WHITE) {
		hdr_meta->white.x = meta_clamp(cc->white.x, "white",
					       0.0, 1.0, output);
		hdr_meta->white.y = meta_clamp(cc->white.y, "white",
					       0.0, 1.0, output);
		hdr_meta->group_mask |= WESTON_HDR_METADATA_TYPE1_GROUP_WHITE;
	}

	/* Target content peak and max mastering luminance */
	if (cc->group_mask & WESTON_COLOR_CHARACTERISTICS_GROUP_MAXL) {
		hdr_meta->maxDML = meta_clamp(cc->max_luminance, "maxDML",
					      1.0, 65535.0, output);
		hdr_meta->maxCLL = meta_clamp(cc->max_luminance, "maxCLL",
					      1.0, 65535.0, output);
		hdr_meta->group_mask |= WESTON_HDR_METADATA_TYPE1_GROUP_MAXDML;
		hdr_meta->group_mask |= WESTON_HDR_METADATA_TYPE1_GROUP_MAXCLL;
	}

	/* Target content min mastering luminance */
	if (cc->group_mask & WESTON_COLOR_CHARACTERISTICS_GROUP_MINL) {
		hdr_meta->minDML = meta_clamp(cc->min_luminance, "minDML",
					      0.0001, 6.5535, output);
		hdr_meta->group_mask |= WESTON_HDR_METADATA_TYPE1_GROUP_MINDML;
	}

	/* Target content max frame-average luminance */
	if (cc->group_mask & WESTON_COLOR_CHARACTERISTICS_GROUP_MAXFALL) {
		hdr_meta->maxFALL = meta_clamp(cc->maxFALL, "maxFALL",
					       1.0, 65535.0, output);
		hdr_meta->group_mask |= WESTON_HDR_METADATA_TYPE1_GROUP_MAXFALL;
	}

	return true;
}

static struct weston_output_color_outcome *
cmlcms_create_output_color_outcome(struct weston_color_manager *cm_base,
				   struct weston_output *output)
{
	struct weston_color_manager_lcms *cm = to_cmlcms(cm_base);
	struct weston_output_color_outcome *co;

	co = zalloc(sizeof *co);
	if (!co)
		return NULL;

	if (!cmlcms_get_hdr_meta(output, &co->hdr_meta))
		goto out_fail;

	assert(output->color_profile);

	/* TODO: take container color space into account */

	if (!cmlcms_get_blend_to_output_color_transform(cm, output,
							&co->from_blend_to_output))
		goto out_fail;

	if (!cmlcms_get_sRGB_to_blend_color_transform(cm, output,
						      &co->from_sRGB_to_blend))
		goto out_fail;

	if (!cmlcms_get_sRGB_to_output_color_transform(cm, output,
						       &co->from_sRGB_to_output))
		goto out_fail;

	return co;

out_fail:
	weston_output_color_outcome_destroy(&co);
	return NULL;
}

static void
transforms_scope_new_sub(struct weston_log_subscription *subs, void *data)
{
	struct weston_color_manager_lcms *cm = data;
	struct cmlcms_color_transform *xform;
	char *str;

	if (wl_list_empty(&cm->color_transform_list))
		return;

	weston_log_subscription_printf(subs, "Existent:\n");
	wl_list_for_each(xform, &cm->color_transform_list, link) {
		weston_log_subscription_printf(subs, "Color transformation t%u:\n", xform->base.id);

		str = cmlcms_color_transform_search_param_string(&xform->search_key);
		weston_log_subscription_printf(subs, "%s", str);
		free(str);

		str = weston_color_transform_string(&xform->base);
		weston_log_subscription_printf(subs, "  %s", str);
		free(str);
	}
}

static void
profiles_scope_new_sub(struct weston_log_subscription *subs, void *data)
{
	struct weston_color_manager_lcms *cm = data;
	struct cmlcms_color_profile *cprof;
	char *str;

	if (wl_list_empty(&cm->color_profile_list))
		return;

	weston_log_subscription_printf(subs, "Existent:\n");
	wl_list_for_each(cprof, &cm->color_profile_list, link) {
		weston_log_subscription_printf(subs, "Color profile p%u:\n", cprof->base.id);

		str = cmlcms_color_profile_print(cprof);
		weston_log_subscription_printf(subs, "%s", str);
		free(str);
	}
}

static void
lcms_error_logger(cmsContext context_id,
		  cmsUInt32Number error_code,
		  const char *text)
{
	weston_log("LittleCMS error: %s\n", text);
}

static bool
cmlcms_init(struct weston_color_manager *cm_base)
{
	struct weston_color_manager_lcms *cm = to_cmlcms(cm_base);
	struct weston_compositor *compositor = cm->base.compositor;

	if (!(compositor->capabilities & WESTON_CAP_COLOR_OPS)) {
		weston_log("color-lcms: error: color operations capability missing. Is GL-renderer not in use?\n");
		return false;
	}

	cm->transforms_scope =
		weston_compositor_add_log_scope(compositor, "color-lcms-transformations",
						"Color transformation creation and destruction.\n",
						transforms_scope_new_sub, NULL, cm);
	weston_assert_ptr(compositor, cm->transforms_scope);

	cm->optimizer_scope =
		weston_compositor_add_log_scope(compositor, "color-lcms-optimizer",
						"Color transformation pipeline optimizer. It's best " \
						"used together with the color-lcms-transformations " \
						"log scope.\n", NULL, NULL, NULL);
	weston_assert_ptr(compositor, cm->optimizer_scope);

	cm->profiles_scope =
		weston_compositor_add_log_scope(compositor, "color-lcms-profiles",
						"Color profile creation and destruction.\n",
						profiles_scope_new_sub, NULL, cm);
	weston_assert_ptr(compositor, cm->profiles_scope);

	cm->lcms_ctx = cmsCreateContext(NULL, cm);
	if (!cm->lcms_ctx) {
		weston_log("color-lcms error: creating LittCMS context failed.\n");
		goto out_err;
	}

	cmsSetLogErrorHandlerTHR(cm->lcms_ctx, lcms_error_logger);

	if (!cmlcms_create_stock_profile(cm)) {
		weston_log("color-lcms: error: cmlcms_create_stock_profile failed\n");
		goto out_err;
	}
	weston_log("LittleCMS %d initialized.\n", cmsGetEncodedCMMversion());

	return true;

out_err:
	if (cm->lcms_ctx)
		cmsDeleteContext(cm->lcms_ctx);
	cm->lcms_ctx = NULL;

	weston_log_scope_destroy(cm->transforms_scope);
	cm->transforms_scope = NULL;
	weston_log_scope_destroy(cm->optimizer_scope);
	cm->optimizer_scope = NULL;
	weston_log_scope_destroy(cm->profiles_scope);
	cm->profiles_scope = NULL;

	return false;
}

static void
cmlcms_destroy(struct weston_color_manager *cm_base)
{
	struct weston_color_manager_lcms *cm = to_cmlcms(cm_base);

	if (cm->sRGB_profile) {
		/* TODO: when we fix the ugly bug described below, we should
		 * change this assert to == 1. */
		weston_assert_true(cm->base.compositor,
				   cm->sRGB_profile->base.ref_count >= 1);
		unref_cprof(cm->sRGB_profile);
	}

	/* TODO: this is an ugly hack. Remove it when we stop leaking surfaces
	 * when shutting down Weston with client surfaces alive. */
	if (!wl_list_empty(&cm->color_profile_list)) {
		struct cmlcms_color_profile *cprof, *tmp;

		weston_log("BUG: When Weston is shutting down with client surfaces alive, it may\n" \
			   "leak them. This is a bug that needs to be fixed. At this point (in which\n" \
			   "we are destroying the color manager), we expect all the objects referencing\n" \
			   "color profiles to be already gone and, consequently, the color profiles\n" \
			   "themselves should have been already destroyed. But because of this other\n" \
			   "bug, this didn't happen, and now we destroy the color profiles and leave\n" \
			   "dangling pointers around.");

		wl_list_for_each_safe(cprof, tmp, &cm->color_profile_list, link)
			cmlcms_color_profile_destroy(cprof);
	}

	assert(wl_list_empty(&cm->color_transform_list));
	assert(wl_list_empty(&cm->color_profile_list));

	if (cm->lcms_ctx)
		cmsDeleteContext(cm->lcms_ctx);

	weston_log_scope_destroy(cm->transforms_scope);
	weston_log_scope_destroy(cm->optimizer_scope);
	weston_log_scope_destroy(cm->profiles_scope);

	free(cm);
}

WL_EXPORT struct weston_color_manager *
weston_color_manager_create(struct weston_compositor *compositor)
{
	struct weston_color_manager_lcms *cm;

	cm = zalloc(sizeof *cm);
	if (!cm)
		return NULL;

	cm->base.name = "work-in-progress";
	cm->base.compositor = compositor;
	cm->base.supports_client_protocol = true;
	cm->base.init = cmlcms_init;
	cm->base.destroy = cmlcms_destroy;
	cm->base.destroy_color_profile = cmlcms_destroy_color_profile;
	cm->base.ref_stock_sRGB_color_profile = cmlcms_ref_stock_sRGB_color_profile;
	cm->base.get_color_profile_from_icc = cmlcms_get_color_profile_from_icc;
	cm->base.get_color_profile_from_params = cmlcms_get_color_profile_from_params;
	cm->base.send_image_desc_info = cmlcms_send_image_desc_info;
	cm->base.destroy_color_transform = cmlcms_destroy_color_transform;
	cm->base.get_surface_color_transform = cmlcms_get_surface_color_transform;
	cm->base.create_output_color_outcome = cmlcms_create_output_color_outcome;

	/* We still do not support creating parametric color profiles. */
	cm->base.supported_color_features = (1 << WESTON_COLOR_FEATURE_ICC);

	/* We support all rendering intents. */
	cm->base.supported_rendering_intents = (1 << WESTON_RENDER_INTENT_PERCEPTUAL) |
					       (1 << WESTON_RENDER_INTENT_SATURATION) |
					       (1 << WESTON_RENDER_INTENT_ABSOLUTE) |
					       (1 << WESTON_RENDER_INTENT_RELATIVE) |
					       (1 << WESTON_RENDER_INTENT_RELATIVE_BPC);

	/* We support all primaries named. */
	cm->base.supported_primaries_named = (1 << WESTON_PRIMARIES_CICP_SRGB) |
					     (1 << WESTON_PRIMARIES_CICP_PAL_M) |
					     (1 << WESTON_PRIMARIES_CICP_PAL) |
					     (1 << WESTON_PRIMARIES_CICP_NTSC) |
					     (1 << WESTON_PRIMARIES_CICP_GENERIC_FILM) |
					     (1 << WESTON_PRIMARIES_CICP_BT2020) |
					     (1 << WESTON_PRIMARIES_CICP_CIE1931_XYZ) |
					     (1 << WESTON_PRIMARIES_CICP_DCI_P3) |
					     (1 << WESTON_PRIMARIES_CICP_DISPLAY_P3) |
					     (1 << WESTON_PRIMARIES_ADOBE_RGB);

	/* We still don't support any tf named. */
	cm->base.supported_tf_named = 0;

	wl_list_init(&cm->color_transform_list);
	wl_list_init(&cm->color_profile_list);

	return &cm->base;
}
