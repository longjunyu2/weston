/*
 * Copyright 2019 Sebastian Wick
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

#include <stdio.h>
#include <string.h>
#include <libweston/libweston.h>

#include "color.h"
#include "color-lcms.h"
#include "color-management.h"
#include "shared/helpers.h"
#include "shared/string-helpers.h"
#include "shared/xalloc.h"
#include "shared/weston-assert.h"

struct xyz_arr_flt {
	float v[3];
};

static double
xyz_dot_prod(const struct xyz_arr_flt a, const struct xyz_arr_flt b)
{
	return (double)a.v[0] * b.v[0] +
	       (double)a.v[1] * b.v[1] +
	       (double)a.v[2] * b.v[2];
}

/**
 * Graeme sketched a linearization method there:
 * https://lists.freedesktop.org/archives/wayland-devel/2019-March/040171.html
 */
static bool
build_eotf_from_clut_profile(cmsContext lcms_ctx,
			     struct lcmsProfilePtr profile,
			     cmsToneCurve *output_eotf[3],
			     int num_points)
{
	int ch, point;
	float *curve_array[3];
	float *red = NULL;
	struct lcmsProfilePtr xyz_profile = { NULL };
	cmsHTRANSFORM transform_rgb_to_xyz = NULL;
	bool ret = false;
	const float div = num_points - 1;

	red = malloc(sizeof(float) * num_points * 3);
	if (!red)
		goto release;

	curve_array[0] = red;
	curve_array[1] = red + num_points;
	curve_array[2] = red + 2 * num_points;

	xyz_profile.p = cmsCreateXYZProfileTHR(lcms_ctx);
	if (!xyz_profile.p)
		goto release;

	transform_rgb_to_xyz = cmsCreateTransformTHR(lcms_ctx, profile.p,
						     TYPE_RGB_FLT, xyz_profile.p,
						     TYPE_XYZ_FLT,
						     INTENT_ABSOLUTE_COLORIMETRIC,
						     0);
	if (!transform_rgb_to_xyz)
		goto release;

	for (ch = 0; ch < 3; ch++) {
		struct xyz_arr_flt prim_xyz_max;
		struct xyz_arr_flt prim_xyz;
		double xyz_square_magnitude;
		float rgb[3] = { 0.0f, 0.0f, 0.0f };

		rgb[ch] = 1.0f;
		cmsDoTransform(transform_rgb_to_xyz, rgb, prim_xyz_max.v, 1);

		/**
		 * Calculate xyz square of magnitude uses single channel 100% and
		 * others are zero.
		 */
		xyz_square_magnitude = xyz_dot_prod(prim_xyz_max, prim_xyz_max);
		/**
		 * Build rgb tone curves
		 */
		for (point = 0; point < num_points; point++) {
			rgb[ch] = (float)point / div;
			cmsDoTransform(transform_rgb_to_xyz, rgb, prim_xyz.v, 1);
			curve_array[ch][point] = xyz_dot_prod(prim_xyz,
							      prim_xyz_max) /
						 xyz_square_magnitude;
		}

		/**
		 * Create LCMS object of rgb tone curves and validate whether
		 * monotonic
		 */
		output_eotf[ch] = cmsBuildTabulatedToneCurveFloat(lcms_ctx,
								  num_points,
								  curve_array[ch]);
		if (!output_eotf[ch])
			goto release;
		if (!cmsIsToneCurveMonotonic(output_eotf[ch])) {
			/**
			 * It is interesting to see how this profile was created.
			 * We assume that such a curve could not be used for linearization
			 * of arbitrary profile.
			 */
			goto release;
		}
	}
	ret = true;

release:
	if (transform_rgb_to_xyz)
		cmsDeleteTransform(transform_rgb_to_xyz);
	if (xyz_profile.p)
		cmsCloseProfile(xyz_profile.p);
	free(red);
	if (ret == false)
		cmsFreeToneCurveTriple(output_eotf);

	return ret;
}

/**
 * Concatenation of two monotonic tone curves.
 * LCMS  API cmsJoinToneCurve does y = Y^-1(X(t)),
 * but want to have y = Y^(X(t))
 */
cmsToneCurve *
lcmsJoinToneCurve(cmsContext context_id, const cmsToneCurve *X,
		  const cmsToneCurve *Y, unsigned int resulting_points)
{
	cmsToneCurve *out = NULL;
	float t, x;
	float *res = NULL;
	unsigned int i;

	res = zalloc(resulting_points * sizeof(float));
	if (res == NULL)
		goto error;

	for (i = 0; i < resulting_points; i++) {
		t = (float)i / (resulting_points - 1);
		x = cmsEvalToneCurveFloat(X, t);
		res[i] = cmsEvalToneCurveFloat(Y, x);
	}

	out = cmsBuildTabulatedToneCurveFloat(context_id, resulting_points, res);

error:
	if (res != NULL)
		free(res);

	return out;
}
/**
 * Extract EOTF from matrix-shaper and cLUT profiles,
 * then invert and concatenate with 'vcgt' curve if it
 * is available.
 */
static bool
ensure_output_profile_extract_icc(struct cmlcms_output_profile_extract *extract,
				  cmsContext lcms_ctx,
				  struct lcmsProfilePtr hProfile,
				  unsigned int num_points,
				  const char **err_msg)
{
	cmsToneCurve *curve = NULL;
	cmsToneCurve **vcgt_curves;
	cmsToneCurve *eotf_curves[3] = {};
	cmsToneCurve *inv_eotf_curves[3] = {};
	unsigned i;
	cmsTagSignature tags[] = {
			cmsSigRedTRCTag, cmsSigGreenTRCTag, cmsSigBlueTRCTag
	};

	if (cmsIsMatrixShaper(hProfile.p)) {
		/**
		 * Matrix-shaper profiles contain TRC and MatrixColumn tags.
		 * Assumes that AToB or DToB tags do not exist or are
		 * equivalent to TRC + MatrixColumn.
		 * We can take the TRC curves straight as EOTF.
		 */
		for (i = 0 ; i < 3; i++) {
			curve = cmsReadTag(hProfile.p, tags[i]);
			if (!curve) {
				*err_msg = "TRC tag missing from matrix-shaper ICC profile";
				goto fail;
			}
			eotf_curves[i] = cmsDupToneCurve(curve);
			if (!eotf_curves[i]) {
				*err_msg = "out of memory";
				goto fail;
			}
		}
	} else {
		/**
		 * Any other kind of profile goes through approximate
		 * linearization that produces sampled curves.
		 */
		if (!build_eotf_from_clut_profile(lcms_ctx, hProfile,
						  eotf_curves, num_points)) {
			*err_msg = "estimating EOTF failed";
			goto fail;
		}
	}

	extract->eotf.p = cmsCreateLinearizationDeviceLinkTHR(lcms_ctx, cmsSigRgbData, eotf_curves);
	if (!extract->eotf.p) {
		*err_msg = "out of memory";
		goto fail;
	}

	for (i = 0; i < 3; i++) {
		curve = cmsReverseToneCurve(eotf_curves[i]);
		if (!curve) {
			*err_msg = "inverting EOTF failed";
			goto fail;
		}
		inv_eotf_curves[i] = curve;
	}
	extract->inv_eotf.p = cmsCreateLinearizationDeviceLinkTHR(lcms_ctx, cmsSigRgbData, inv_eotf_curves);
	if (!extract->inv_eotf.p) {
		*err_msg = "out of memory";
		goto fail;
	}

	vcgt_curves = cmsReadTag(hProfile.p, cmsSigVcgtTag);
	if (vcgt_curves && vcgt_curves[0] && vcgt_curves[1] && vcgt_curves[2]) {
		extract->vcgt.p = cmsCreateLinearizationDeviceLinkTHR(lcms_ctx, cmsSigRgbData, vcgt_curves);
		if (!extract->vcgt.p) {
			*err_msg = "out of memory";
			goto fail;
		}
	}

	cmsFreeToneCurveTriple(inv_eotf_curves);
	cmsFreeToneCurveTriple(eotf_curves);

	return true;

fail:
	cmsCloseProfile(extract->vcgt.p);
	extract->vcgt.p = NULL;

	cmsCloseProfile(extract->inv_eotf.p);
	extract->inv_eotf.p = NULL;

	cmsCloseProfile(extract->eotf.p);
	extract->eotf.p = NULL;

	cmsFreeToneCurveTriple(inv_eotf_curves);
	cmsFreeToneCurveTriple(eotf_curves);

	return false;
}

bool
ensure_output_profile_extract(struct cmlcms_color_profile *cprof,
			      cmsContext lcms_ctx,
			      unsigned int num_points,
			      const char **err_msg)
{
	struct weston_compositor *compositor = cprof->base.cm->compositor;
	bool ret;

	/* Everything already computed */
	if (cprof->extract.eotf.p)
		return true;

	switch (cprof->type) {
	case CMLCMS_PROFILE_TYPE_ICC:
		ret = ensure_output_profile_extract_icc(&cprof->extract, lcms_ctx,
							cprof->icc.profile, num_points,
							err_msg);
		if (ret)
			weston_assert_ptr(compositor, cprof->extract.eotf.p);
		break;
	case CMLCMS_PROFILE_TYPE_PARAMS:
		/* TODO: need to address this when we create param profiles. */
		ret = false;
		break;
	default:
		weston_assert_not_reached(compositor, "unknown profile type");
	}

	return ret;
}

static const char *
icc_profile_class_name(cmsProfileClassSignature s)
{
	switch (s) {
	case cmsSigInputClass: return "Input";
	case cmsSigDisplayClass: return "Display";
	case cmsSigOutputClass: return "Output";
	case cmsSigLinkClass: return "Link";
	case cmsSigAbstractClass: return "Abstract";
	case cmsSigColorSpaceClass: return "ColorSpace";
	case cmsSigNamedColorClass: return "NamedColor";
	default: return "(unknown)";
	}
}

/* FIXME: sync with spec! */
static bool
validate_icc_profile(struct lcmsProfilePtr profile, char **errmsg)
{
	cmsColorSpaceSignature cs = cmsGetColorSpace(profile.p);
	uint32_t nr_channels = cmsChannelsOf(cs);
	uint8_t version = cmsGetEncodedICCversion(profile.p) >> 24;
	cmsProfileClassSignature class_sig = cmsGetDeviceClass(profile.p);

	if (version != 2 && version != 4) {
		str_printf(errmsg,
			   "ICC profile major version %d is unsupported, should be 2 or 4.",
			   version);
		return false;
	}

	if (nr_channels != 3) {
		str_printf(errmsg,
			   "ICC profile must contain 3 channels for the color space, not %u.",
			   nr_channels);
		return false;
	}

	if (class_sig != cmsSigDisplayClass) {
		str_printf(errmsg, "ICC profile is required to be of Display device class, "
				   "but it is %s class (0x%08x)",
			   icc_profile_class_name(class_sig), (unsigned)class_sig);
		return false;
	}

	return true;
}

static struct cmlcms_color_profile *
cmlcms_find_color_profile_by_md5(const struct weston_color_manager_lcms *cm,
				 const struct cmlcms_md5_sum *md5sum)
{
	struct cmlcms_color_profile *cprof;

	wl_list_for_each(cprof, &cm->color_profile_list, link) {
		if (cprof->type != CMLCMS_PROFILE_TYPE_ICC)
			continue;

		if (memcmp(cprof->icc.md5sum.bytes,
			   md5sum->bytes, sizeof(md5sum->bytes)) == 0)
			return cprof;
	}

	return NULL;
}

static struct cmlcms_color_profile *
cmlcms_find_color_profile_by_params(const struct weston_color_manager_lcms *cm,
				    const struct weston_color_profile_params *params)
{
	struct cmlcms_color_profile *cprof;

	/* Ensure no uninitialized data inside struct to make memcmp work. */
	static_assert(sizeof(*params) ==
			2 * sizeof(float) * 2 * 4 + /* primaries, target_primaries */
			sizeof(params->primaries_info) +
			sizeof(params->tf_info) +
			sizeof(params->tf_params) +
			sizeof(params->min_luminance) +
			sizeof(params->max_luminance) +
			sizeof(params->maxCLL) +
			sizeof(params->maxFALL),
		"struct weston_color_profile_params must not contain implicit padding");

	wl_list_for_each(cprof, &cm->color_profile_list, link) {
		if (cprof->type != CMLCMS_PROFILE_TYPE_PARAMS)
			continue;

		if (memcmp(cprof->params, params, sizeof(*params)) == 0)
			return cprof;
	}

	return NULL;
}

char *
cmlcms_color_profile_print(const struct cmlcms_color_profile *cprof)
{
	char *str;

	/* TODO: also print cprof->params for parametric profiles. */

	str_printf(&str, "  description: %s\n", cprof->base.description);
	abort_oom_if_null(str);

	return str;
}

static struct cmlcms_color_profile *
cmlcms_color_profile_create(struct weston_color_manager_lcms *cm,
			    struct lcmsProfilePtr profile,
			    char *desc,
			    char **errmsg)
{
	struct cmlcms_color_profile *cprof;
	char *str;

	cprof = zalloc(sizeof *cprof);
	if (!cprof)
		return NULL;

	weston_color_profile_init(&cprof->base, &cm->base);
	cprof->base.description = desc;
	cprof->icc.profile = profile;
	cmsGetHeaderProfileID(profile.p, cprof->icc.md5sum.bytes);
	wl_list_insert(&cm->color_profile_list, &cprof->link);

	weston_log_scope_printf(cm->profiles_scope,
				"New color profile: p%u\n", cprof->base.id);

	str = cmlcms_color_profile_print(cprof);
	weston_log_scope_printf(cm->profiles_scope, "%s", str);
	free(str);

	return cprof;
}

void
cmlcms_color_profile_destroy(struct cmlcms_color_profile *cprof)
{
	struct weston_color_manager_lcms *cm = to_cmlcms(cprof->base.cm);

	wl_list_remove(&cprof->link);
	cmsCloseProfile(cprof->extract.vcgt.p);
	cmsCloseProfile(cprof->extract.inv_eotf.p);
	cmsCloseProfile(cprof->extract.eotf.p);

	switch (cprof->type) {
	case CMLCMS_PROFILE_TYPE_ICC:
		cmsCloseProfile(cprof->icc.profile.p);
		/**
		 * TODO: drop this if when we convert the stock sRGB profile to
		 * a parametric one. When we do that, all ICC profiles will have
		 * their ro_anonymous_file, so we won't have to check.
		 */
		if (cprof->icc.prof_rofile)
			os_ro_anonymous_file_destroy(cprof->icc.prof_rofile);
		break;
	case CMLCMS_PROFILE_TYPE_PARAMS:
		free(cprof->params);
		break;
	default:
		weston_assert_not_reached(cm->base.compositor,
					  "unknown profile type");
	}

	weston_log_scope_printf(cm->profiles_scope, "Destroyed color profile p%u. " \
				"Description: %s\n", cprof->base.id, cprof->base.description);

	free(cprof->base.description);
	free(cprof);
}

struct cmlcms_color_profile *
ref_cprof(struct cmlcms_color_profile *cprof)
{
	if (!cprof)
		return NULL;

	weston_color_profile_ref(&cprof->base);
	return  cprof;
}

void
unref_cprof(struct cmlcms_color_profile *cprof)
{
	if (!cprof)
		return;

	weston_color_profile_unref(&cprof->base);
}

static char *
make_icc_file_description(struct lcmsProfilePtr profile,
			  const struct cmlcms_md5_sum *md5sum,
			  const char *name_part)
{
	char md5sum_str[sizeof(md5sum->bytes) * 2 + 1];
	char *desc;
	size_t i;

	for (i = 0; i < sizeof(md5sum->bytes); i++) {
		snprintf(md5sum_str + 2 * i, sizeof(md5sum_str) - 2 * i,
			 "%02x", md5sum->bytes[i]);
	}

	str_printf(&desc, "ICCv%.1f %s %s", cmsGetProfileVersion(profile.p),
		   name_part, md5sum_str);

	return desc;
}

/**
 *
 * Build stock profile which available for clients unaware of color management
 */
bool
cmlcms_create_stock_profile(struct weston_color_manager_lcms *cm)
{
	struct lcmsProfilePtr profile;
	struct cmlcms_md5_sum md5sum;
	char *desc = NULL;
	const char *err_msg = NULL;

	profile.p = cmsCreate_sRGBProfileTHR(cm->lcms_ctx);
	if (!profile.p) {
		weston_log("color-lcms: error: cmsCreate_sRGBProfileTHR failed\n");
		return false;
	}
	if (!cmsMD5computeID(profile.p)) {
		weston_log("Failed to compute MD5 for ICC profile\n");
		goto err_close;
	}

	cmsGetHeaderProfileID(profile.p, md5sum.bytes);
	desc = make_icc_file_description(profile, &md5sum, "sRGB stock");
	if (!desc)
		goto err_close;

	cm->sRGB_profile = cmlcms_color_profile_create(cm, profile, desc, NULL);
	if (!cm->sRGB_profile)
		goto err_close;

	cm->sRGB_profile->type = CMLCMS_PROFILE_TYPE_ICC;

	if (!ensure_output_profile_extract(cm->sRGB_profile, cm->lcms_ctx,
					   cmlcms_reasonable_1D_points(), &err_msg))
		goto err_close;

	return true;

err_close:
	if (err_msg)
		weston_log("%s\n", err_msg);

	free(desc);
	cmsCloseProfile(profile.p);
	return false;
}

struct weston_color_profile *
cmlcms_ref_stock_sRGB_color_profile(struct weston_color_manager *cm_base)
{
	struct weston_color_manager_lcms *cm = to_cmlcms(cm_base);
	struct cmlcms_color_profile *cprof;

	cprof = ref_cprof(cm->sRGB_profile);

	return &cprof->base;
}

bool
cmlcms_get_color_profile_from_icc(struct weston_color_manager *cm_base,
				  const void *icc_data,
				  size_t icc_len,
				  const char *name_part,
				  struct weston_color_profile **cprof_out,
				  char **errmsg)
{
	struct weston_color_manager_lcms *cm = to_cmlcms(cm_base);
	struct lcmsProfilePtr profile;
	struct cmlcms_md5_sum md5sum;
	struct ro_anonymous_file *prof_rofile = NULL;
	struct cmlcms_color_profile *cprof = NULL;
	char *desc = NULL;

	if (!icc_data || icc_len < 1) {
		str_printf(errmsg, "No ICC data.");
		return false;
	}
	if (icc_len >= UINT32_MAX) {
		str_printf(errmsg, "Too much ICC data.");
		return false;
	}

	profile.p = cmsOpenProfileFromMemTHR(cm->lcms_ctx, icc_data, icc_len);
	if (!profile.p) {
		str_printf(errmsg, "ICC data not understood.");
		return false;
	}

	if (!validate_icc_profile(profile, errmsg))
		goto err_close;

	if (!cmsMD5computeID(profile.p)) {
		str_printf(errmsg, "Failed to compute MD5 for ICC profile.");
		goto err_close;
	}

	cmsGetHeaderProfileID(profile.p, md5sum.bytes);
	cprof = cmlcms_find_color_profile_by_md5(cm, &md5sum);
	if (cprof) {
		*cprof_out = weston_color_profile_ref(&cprof->base);
		cmsCloseProfile(profile.p);
		return true;
	}

	desc = make_icc_file_description(profile, &md5sum, name_part);
	if (!desc)
		goto err_close;

	prof_rofile = os_ro_anonymous_file_create(icc_len, icc_data);
	if (!prof_rofile)
		goto err_close;

	cprof = cmlcms_color_profile_create(cm, profile, desc, errmsg);
	if (!cprof)
		goto err_close;

	cprof->type = CMLCMS_PROFILE_TYPE_ICC;
	cprof->icc.prof_rofile = prof_rofile;

	*cprof_out = &cprof->base;
	return true;

err_close:
	if (prof_rofile)
		os_ro_anonymous_file_destroy(prof_rofile);
	if (cprof)
		cmlcms_color_profile_destroy(cprof);
	free(desc);
	cmsCloseProfile(profile.p);
	return false;
}

bool
cmlcms_get_color_profile_from_params(struct weston_color_manager *cm_base,
				     const struct weston_color_profile_params *params,
				     const char *name_part,
				     struct weston_color_profile **cprof_out,
				     char **errmsg)
{
	struct weston_color_manager_lcms *cm = to_cmlcms(cm_base);
	struct cmlcms_color_profile *cprof;
	char *desc;
	char *str;

	/* TODO: add a helper similar to cmlcms_color_profile_create() but for
	 * parametric color profiles. For now this just creates a cprof
	 * boilerplate, just to help us to imagine how things would work.
	 *
	 * The color profile that this function creates is invalid and we won't
	 * be able to do anything useful with that.
	 */

	cprof = cmlcms_find_color_profile_by_params(cm, params);
	if (cprof) {
		*cprof_out = weston_color_profile_ref(&cprof->base);
		return true;
	}

	cprof = xzalloc(sizeof(*cprof));
	cprof->type = CMLCMS_PROFILE_TYPE_PARAMS;

	cprof->params = xzalloc(sizeof(*cprof->params));
	memcpy(cprof->params, params, sizeof(*params));

	str_printf(&desc, "Parametric (%s): %s, %s",
			  name_part,
			  params->primaries_info ? params->primaries_info->desc :
						   "custom primaries",
			  params->tf_info->desc);

	weston_color_profile_init(&cprof->base, &cm->base);
	cprof->base.description = desc;
	wl_list_insert(&cm->color_profile_list, &cprof->link);

	weston_log_scope_printf(cm->profiles_scope,
				"New color profile: p%u. WARNING: this is a " \
				"boilerplate color profile. We still do not fully " \
				"support creating color profiles from params\n",
				cprof->base.id);

	str = cmlcms_color_profile_print(cprof);
	weston_log_scope_printf(cm->profiles_scope, "%s", str);
	free(str);

	*cprof_out = &cprof->base;
	return true;
}

bool
cmlcms_send_image_desc_info(struct cm_image_desc_info *cm_image_desc_info,
			    struct weston_color_profile *cprof_base)
{
	struct weston_color_manager_lcms *cm = to_cmlcms(cprof_base->cm);
	struct weston_compositor *compositor = cm->base.compositor;
	struct cmlcms_color_profile *cprof = to_cmlcms_cprof(cprof_base);
	const struct weston_color_primaries_info *primaries_info;
	const struct weston_color_tf_info *tf_info;
        int32_t fd;
        uint32_t len;

	/**
	 * TODO: when we convert the stock sRGB profile to a parametric profile
	 * instead of an ICC one, we'll be able to change the if/else below to
	 * a switch/case.
	 */

	if (cprof->type == CMLCMS_PROFILE_TYPE_ICC && cprof != cm->sRGB_profile) {
		/* ICC-based color profile, so just send the ICC file fd. If we
		 * get an error (negative fd), the helper will send the proper
		 * error to the client. */
		fd = os_ro_anonymous_file_get_fd(cprof->icc.prof_rofile,
						 RO_ANONYMOUS_FILE_MAPMODE_PRIVATE);
		if (fd < 0) {
			weston_cm_send_icc_file(cm_image_desc_info, -1, 0);
			return false;
		}

		len = os_ro_anonymous_file_size(cprof->icc.prof_rofile);
		weston_assert_uint32_gt(compositor, len, 0);

		weston_cm_send_icc_file(cm_image_desc_info, fd, len);

		os_ro_anonymous_file_put_fd(fd);
	} else {
		/* TODO: we still don't support parametric color profiles that
		 * are not the stock one. This should change when we start
		 * advertising parametric image description support in our
		 * color-management protocol implementation. */
		if (cprof != cm->sRGB_profile)
			weston_assert_not_reached(compositor, "we don't support parametric " \
						  "cprof's that are not the stock sRGB one");

		/* Stock sRGB color profile. TODO: when we add support for
		 * parametric color profiles, the stock sRGB will be crafted
		 * using parameters, instead of cmsCreate_sRGBProfileTHR()
		 * (which we currently use). So we'll get the parameters
		 * directly from it, instead of hardcoding as we are doing here.
		 * We don't get the parameters from the stock sRGB color profile
		 * because it is not trivial to retrieve that from LittleCMS. */

		/* Send the H.273 ColourPrimaries code point that matches the
		 * Rec709 primaries and the D65 white point. */
		primaries_info = weston_color_primaries_info_from(compositor,
								  WESTON_PRIMARIES_CICP_SRGB);
		weston_cm_send_primaries_named(cm_image_desc_info, primaries_info);

		/* These are the Rec709 primaries and D65 white point. */
		weston_cm_send_primaries(cm_image_desc_info,
					 &primaries_info->color_gamut);

		/* sRGB transfer function. */
		tf_info = weston_color_tf_info_from(compositor, WESTON_TF_GAMMA22);
		weston_cm_send_tf_named(cm_image_desc_info, tf_info);
	}

	return true;
}

void
cmlcms_destroy_color_profile(struct weston_color_profile *cprof_base)
{
	struct cmlcms_color_profile *cprof = to_cmlcms_cprof(cprof_base);

	cmlcms_color_profile_destroy(cprof);
}
