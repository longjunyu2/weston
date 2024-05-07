/*
 * Copyright 2024 Collabora, Ltd.
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

#include "color.h"
#include "color-properties.h"
#include "shared/helpers.h"
#include "shared/string-helpers.h"
#include "shared/xalloc.h"
#include "shared/weston-assert.h"

/** Enum that helps keep track of what params have been set. */
enum weston_color_profile_params_set {
	WESTON_COLOR_PROFILE_PARAMS_PRIMARIES = 0x01,
	WESTON_COLOR_PROFILE_PARAMS_TF = 0x02,
	WESTON_COLOR_PROFILE_PARAMS_TARGET_PRIMARIES = 0x04,
	WESTON_COLOR_PROFILE_PARAMS_LUMINANCE = 0x08,
	WESTON_COLOR_PROFILE_PARAMS_MAXCLL = 0x10,
	WESTON_COLOR_PROFILE_PARAMS_MAXFALL = 0x20,
};

/** Builder object to create color profiles with parameters. */
struct weston_color_profile_param_builder {
	struct weston_compositor *compositor;
	struct weston_color_profile_params params;

	/*
	 * Keeps track of what params have already been set.
	 *
	 * A bitmask of values from enum weston_color_profile_params_set.
	 */
	uint32_t group_mask;

	/*
	 * Keeps track of errors.
	 *
	 * This API may produce errors, and we store all the error messages in
	 * the string below to help users to debug. Regarding error codes, we
	 * store only the first that occurs.
	 *
	 * Such errors can be queried with
	 * weston_color_profile_param_builder_get_error(). They are also
	 * return'ed when users of the API set all the params and call
	 * weston_color_profile_param_builder_create_color_profile().
	 */
	enum weston_color_profile_param_builder_error err;
	bool has_errors;
	FILE *err_fp;
	char *err_msg;
	size_t err_msg_size;
};

/**
 * Creates struct weston_color_profile_param_builder object. It should be used
 * to create color profiles from parameters.
 *
 * We expect it to be used by our frontend (to allow creating color profiles
 * from .ini files or similar) and by the color-management protocol
 * implementation (so that clients can create color profiles from parameters).
 *
 * It is invalid to set the same parameter twice using this object.
 *
 * After creating the color profile from this object, it will be automatically
 * destroyed.
 *
 * \param compositor The weston compositor.
 * \return The struct weston_color_profile_param_builder object created.
*/
struct weston_color_profile_param_builder *
weston_color_profile_param_builder_create(struct weston_compositor *compositor)
{
	struct weston_color_profile_param_builder *builder;

	builder = xzalloc(sizeof(*builder));

	builder->compositor = compositor;

	builder->err_fp = open_memstream(&builder->err_msg,
					 &builder->err_msg_size);
	weston_assert_ptr(compositor, builder->err_fp);

	return builder;
}

/**
 * Destroys a struct weston_color_profile_param_builder object.
 *
 * \param builder The object that should be destroyed.
 */
void
weston_color_profile_param_builder_destroy(struct weston_color_profile_param_builder *builder)
{
	fclose(builder->err_fp);
	free(builder->err_msg);
	free(builder);
}

static void __attribute__ ((format (printf, 3, 4)))
store_error(struct weston_color_profile_param_builder *builder,
	    enum weston_color_profile_param_builder_error err,
	    const char *fmt, ...)
{
	va_list ap;

	/* First error that we log. We also log the err code in such case. */
	if (!builder->has_errors) {
		builder->has_errors = true;
		builder->err = err;
		goto log_msg;
	}

	/* There are errors already, so add new line first. */
	fprintf(builder->err_fp, "\n");

log_msg:
	va_start(ap, fmt);
	vfprintf(builder->err_fp, fmt, ap);
	va_end(ap);
}

/**
 * Returns the code for the first error generated and a string with all error
 * messages that we caught.
 *
 * Function weston_color_profile_param_builder_create_color_profile() will also
 * fail with the first error code (if there's any), but we still need this
 * function because some users of the API may want to know about the error
 * immediately after calling a setter.
 *
 * \param builder The builder object whose parameters will be set.
 * \param err Set if there's an error, untouched otherwise. The first error code caught.
 * \param err_msg Set if there's an error, untouched otherwise. Must be free()'d
 * by the caller. Combination of all error messages caught. Not terminated with
 * a new line character.
 * \return true if there's an error, false otherwise.
 */
bool
weston_color_profile_param_builder_get_error(struct weston_color_profile_param_builder *builder,
					     enum weston_color_profile_param_builder_error *err,
					     char **err_msg)
{
	if (!builder->has_errors)
		return false;

	*err = builder->err;

	fflush(builder->err_fp);
	*err_msg = strdup(builder->err_msg);

	return true;
}

/**
 * Sets primaries for struct weston_color_profile_param_builder object.
 *
 * See also weston_color_profile_param_builder_set_primaries_named(), which is
 * another way of setting the primaries.
 *
 * If the primaries are already set (with this function or the one
 * mentioned above), this should fail. Setting a parameter twice is forbidden.
 *
 * If this fails, users can call weston_color_profile_param_builder_get_error()
 * to get the error details.
 *
 * \param builder The builder object whose parameters will be set.
 * \param primaries The object containing the primaries.
 * \return true on success, false otherwise.
 */
bool
weston_color_profile_param_builder_set_primaries(struct weston_color_profile_param_builder *builder,
						 const struct weston_color_gamut *primaries)
{
	struct weston_color_manager *cm = builder->compositor->color_manager;
	bool success = true;

	if (!((cm->supported_color_features >> WESTON_COLOR_FEATURE_SET_PRIMARIES) & 1)) {
		store_error(builder, WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_INVALID_PRIMARIES,
			    "set_primaries not supported by the color manager");
		success = false;
	}

	if (builder->group_mask & WESTON_COLOR_PROFILE_PARAMS_PRIMARIES) {
		store_error(builder, WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_ALREADY_SET,
			    "primaries were already set");
		success = false;
	}

	if (!success)
		return false;

	builder->params.primaries = *primaries;

	builder->group_mask |= WESTON_COLOR_PROFILE_PARAMS_PRIMARIES;

	return true;
}

/**
 * Sets primaries for struct weston_color_profile_param_builder object using a
 * enum weston_color_primaries.
 *
 * See also weston_color_profile_param_builder_set_primaries(), which is another
 * way of setting the primaries.
 *
 * If the primaries are already set (with this function or the one mentioned
 * above), this should fail. Setting a parameter twice is forbidden.
 *
 * If this fails, users can call weston_color_profile_param_builder_get_error()
 * to get the error details.
 *
 * \param builder The builder object whose parameters will be set.
 * \param primaries The enum representing the primaries.
 * \return true on success, false otherwise.
 */
bool
weston_color_profile_param_builder_set_primaries_named(struct weston_color_profile_param_builder *builder,
						       enum weston_color_primaries primaries)
{
	struct weston_compositor *compositor = builder->compositor;
	struct weston_color_manager *cm = compositor->color_manager;
	bool success = true;

	if (!((cm->supported_primaries_named >> primaries) & 1)) {
		store_error(builder, WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_INVALID_PRIMARIES,
			    "named primaries %u not supported by the color manager",
			    primaries);
		success = false;
	}

	if (builder->group_mask & WESTON_COLOR_PROFILE_PARAMS_PRIMARIES) {
		store_error(builder, WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_ALREADY_SET,
			    "primaries were already set");
		success = false;
	}

	if (!success)
		return false;

	builder->params.primaries_info =
		weston_color_primaries_info_from(compositor, primaries);

	builder->params.primaries = builder->params.primaries_info->color_gamut;

	builder->group_mask |= WESTON_COLOR_PROFILE_PARAMS_PRIMARIES;

	return true;
}

/**
 * Sets transfer function for struct weston_color_profile_param_builder object
 * using a enum weston_transfer_function.
 *
 * See also weston_color_profile_param_builder_set_tf_power_exponent(), which is
 * another way of setting the transfer function.
 *
 * If the transfer function is already set (with this function or the one
 * mentioned above), this should fail. Setting a parameter twice is forbidden.
 *
 * If this fails, users can call weston_color_profile_param_builder_get_error()
 * to get the error details.
 *
 * \param builder The builder object whose parameters will be set.
 * \param tf The enum representing the transfer function.
 * \return true on success, false otherwise.
 */
bool
weston_color_profile_param_builder_set_tf_named(struct weston_color_profile_param_builder *builder,
						enum weston_transfer_function tf)
{
	struct weston_compositor *compositor = builder->compositor;
	struct weston_color_manager *cm = compositor->color_manager;
	bool success = true;

	if (!((cm->supported_tf_named >> tf) & 1)) {
		store_error(builder, WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_INVALID_TF,
			    "named tf %u not supported by the color manager", tf);
		success = false;
	}

	if (builder->group_mask & WESTON_COLOR_PROFILE_PARAMS_TF) {
		store_error(builder, WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_ALREADY_SET,
			    "tf was already set");
		success = false;
	}

	if (!success)
		return false;

	builder->params.tf_info = weston_color_tf_info_from(compositor, tf);
	weston_assert_false(builder->compositor,
			    builder->params.tf_info->has_parameters);

	builder->group_mask |= WESTON_COLOR_PROFILE_PARAMS_TF;

	return true;
}

/**
 * Sets transfer function for struct weston_color_profile_param_builder object
 * using a power law function exponent g. In such case, the transfer function is
 * y = x ^ g. The valid range for the given exponent is [1.0, 10.0].
 *
 * See also weston_color_profile_param_builder_set_tf_named(), which is another
 * way of setting the transfer function.
 *
 * If the transfer function is already set (with this function or the one
 * mentioned above), this should fail. Setting a parameter twice is forbidden.
 *
 * If this fails, users can call weston_color_profile_param_builder_get_error()
 * to get the error details.
 *
 * \param builder The builder object whose parameters will be set.
 * \param power_exponent The power law function exponent.
 * \return true on success, false otherwise.
 */
bool
weston_color_profile_param_builder_set_tf_power_exponent(struct weston_color_profile_param_builder *builder,
							 float power_exponent)
{
	struct weston_compositor *compositor = builder->compositor;
	struct weston_color_manager *cm = compositor->color_manager;
	bool success = true;

	if (!((cm->supported_color_features >> WESTON_COLOR_FEATURE_SET_TF_POWER) & 1)) {
		store_error(builder, WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_INVALID_TF,
			    "set_tf_power not supported by the color manager");
		success = false;
	}

	if (builder->group_mask & WESTON_COLOR_PROFILE_PARAMS_TF) {
		store_error(builder, WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_ALREADY_SET,
			    "tf was already set");
		success = false;
	}

	/* The exponent should be at least 1.0 and at most 10.0. */
	if (!(power_exponent >= 1.0 && power_exponent <= 10.0)) {
		store_error(builder, WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_INVALID_TF,
			    "tf power exponent %f is not in the range [1.0, 10.0]",
			    power_exponent);
		success = false;
	}

	if (!success)
		return false;

	builder->params.tf_info = weston_color_tf_info_from(compositor, WESTON_TF_POWER);
	builder->params.tf_params[0] = power_exponent;

	builder->group_mask |= WESTON_COLOR_PROFILE_PARAMS_TF;

	return true;
}

/**
 * Sets target primaries for struct weston_color_profile_param_builder object
 * using raw values.
 *
 * If the target primaries are already set, this should fail. Setting a
 * parameter twice is forbidden.
 *
 * If this fails, users can call weston_color_profile_param_builder_get_error()
 * to get the error details.
 *
 * \param builder The builder object whose parameters will be set.
 * \param target_primaries The object containing the target primaries.
 * \return true on success, false otherwise.
 */
bool
weston_color_profile_param_builder_set_target_primaries(struct weston_color_profile_param_builder *builder,
							const struct weston_color_gamut *target_primaries)
{
	struct weston_color_manager *cm = builder->compositor->color_manager;
	bool success = true;

	if (!((cm->supported_color_features >> WESTON_COLOR_FEATURE_SET_MASTERING_DISPLAY_PRIMARIES) & 1)) {
		store_error(builder, WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_INVALID_TARGET_PRIMARIES,
			    "set_mastering_display_primaries not supported by " \
			    "the color manager");
		success = false;
	}

	if (builder->group_mask & WESTON_COLOR_PROFILE_PARAMS_TARGET_PRIMARIES) {
		store_error(builder, WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_ALREADY_SET,
			    "target primaries were already set");
		success = false;
	}

	if (!success)
		return false;

	builder->params.target_primaries = *target_primaries;

	builder->group_mask |= WESTON_COLOR_PROFILE_PARAMS_TARGET_PRIMARIES;

	return true;
}

/**
 * Sets target luminance for struct weston_color_profile_param_builder object.
 *
 * If the target luminance is already set, this should fail. Setting a parameter
 * twice is forbidden.
 *
 * If this fails, users can call weston_color_profile_param_builder_get_error()
 * to get the error details.
 *
 * \param builder The builder object whose parameters will be set.
 * \param min_luminance The minimum luminance.
 * \param max_luminance The maximum luminance.
 * \return true on success, false otherwise.
 */
bool
weston_color_profile_param_builder_set_target_luminance(struct weston_color_profile_param_builder *builder,
							float min_luminance, float max_luminance)
{
	bool success = true;

	if (builder->group_mask & WESTON_COLOR_PROFILE_PARAMS_LUMINANCE) {
		store_error(builder, WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_ALREADY_SET,
			    "target luminance was already set");
		success = false;
	}

	if (min_luminance >= max_luminance) {
		store_error(builder, WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_INVALID_LUMINANCE,
			    "min luminance %f shouldn't be greater than or equal to max %f",
			    min_luminance, max_luminance);
		success = false;
	}

	if (!success)
		return false;

	builder->params.min_luminance = min_luminance;
	builder->params.max_luminance = max_luminance;

	builder->group_mask |= WESTON_COLOR_PROFILE_PARAMS_LUMINANCE;

	return true;
}

/**
 * Sets target maxFALL for struct weston_color_profile_param_builder object.
 *
 * If the target maxFALL is already set, this should fail. Setting a parameter
 * twice is forbidden.
 *
 * If this fails, users can call weston_color_profile_param_builder_get_error()
 * to get the error details.
 *
 * \param builder The builder object whose parameters will be set.
 * \param maxFALL The maxFALL.
 * \return true on success, false otherwise.
 */
bool
weston_color_profile_param_builder_set_maxFALL(struct weston_color_profile_param_builder *builder,
					       float maxFALL)
{
	if (builder->group_mask & WESTON_COLOR_PROFILE_PARAMS_MAXFALL) {
		store_error(builder, WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_ALREADY_SET,
			    "max fall was already set");
		return false;
	}

	builder->params.maxFALL = maxFALL;

	builder->group_mask |= WESTON_COLOR_PROFILE_PARAMS_MAXFALL;

	return true;
}

/**
 * Sets target maxCLL for struct weston_color_profile_param_builder object.
 *
 * If the target maxCLL is already set, this should fail. Setting a parameter
 * twice is forbidden.
 *
 * If this fails, users can call weston_color_profile_param_builder_get_error()
 * to get the error details.
 *
 * \param builder The builder object whose parameters will be set.
 * \param maxCLL The maxCLL.
 * \return true on success, false otherwise.
 */
bool
weston_color_profile_param_builder_set_maxCLL(struct weston_color_profile_param_builder *builder,
					      float maxCLL)
{
	if (builder->group_mask & WESTON_COLOR_PROFILE_PARAMS_MAXCLL) {
		store_error(builder, WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_ALREADY_SET,
			    "max cll was already set");
		return false;
	}

	builder->params.maxCLL = maxCLL;

	builder->group_mask |= WESTON_COLOR_PROFILE_PARAMS_MAXCLL;

	return true;
}

static void
builder_validate_params_set(struct weston_color_profile_param_builder *builder)
{
	/* Primaries are mandatory. */
	if (!(builder->group_mask & WESTON_COLOR_PROFILE_PARAMS_PRIMARIES))
		store_error(builder, WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_INCOMPLETE_SET,
			    "primaries not set");

	/* TF is mandatory. */
	if (!(builder->group_mask & WESTON_COLOR_PROFILE_PARAMS_TF))
		store_error(builder, WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_INCOMPLETE_SET,
			    "transfer function not set");

	/* If luminance values were given, tf must be PQ. */
	if (builder->params.tf_info->tf != WESTON_TF_ST2084_PQ &&
	    (builder->group_mask & WESTON_COLOR_PROFILE_PARAMS_LUMINANCE ||
	     builder->group_mask & WESTON_COLOR_PROFILE_PARAMS_MAXCLL ||
	     builder->group_mask & WESTON_COLOR_PROFILE_PARAMS_MAXFALL))
		store_error(builder, WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_INCONSISTENT_SET,
			    "luminance values were given but transfer function " \
			    "is not Rec. ITU-R BT.2100-2 (PQ)");
}

static float
triangle_area(float x1, float y1, float x2, float y2, float x3, float y3)
{
	/* Based on the shoelace formula, also known as Gauss's area formula. */
	return fabs((x1 - x3) * (y2 - y1) - (x1 - x2) * (y3 - y1)) / 2.0f;
}

static bool
is_point_inside_triangle(float point_x, float point_y,
			 float x1, float y1, float x2, float y2, float x3, float y3)
{
	float A1, A2, A3;
	float A;
	const float PRECISION = 1e-5;

	A = triangle_area(x1, y1, x2, y2, x3, y3);

	/* Bail out if something that is not a triangle was given. */
	if (A <= PRECISION)
		return false;

	A1 = triangle_area(point_x, point_y, x1, y1, x2, y2);
	A2 = triangle_area(point_x, point_y, x1, y1, x3, y3);
	A3 = triangle_area(point_x, point_y, x2, y2, x3, y3);

	if (fabs(A - (A1 + A2 + A3)) <= PRECISION)
		return true;

	return false;
}

static void
validate_color_gamut(struct weston_color_profile_param_builder *builder,
		     const struct weston_color_gamut *gamut,
		     const char *gamut_name)
{
	/*
	 * We choose the legal range [-1.0, 2.0] for CIE xy values. It is
	 * probably more than we'd ever need, but tight enough to not cause
	 * mathematical issues. If wasn't for the ACES AP0 color space, we'd
	 * probably choose the range [0.0, 1.0].
	 */
	if (gamut->white_point.x < -1.0f || gamut->white_point.x > 2.0f ||
	    gamut->white_point.y < -1.0f || gamut->white_point.y > 2.0f ||
	    gamut->primary[0].x < -1.0f || gamut->primary[0].x > 2.0f ||
	    gamut->primary[0].y < -1.0f || gamut->primary[0].y > 2.0f ||
	    gamut->primary[1].x < -1.0f || gamut->primary[1].x > 2.0f ||
	    gamut->primary[1].y < -1.0f || gamut->primary[1].y > 2.0f ||
	    gamut->primary[2].x < -1.0f || gamut->primary[2].x > 2.0f ||
	    gamut->primary[2].y < -1.0f || gamut->primary[2].y > 2.0f) {
		store_error(builder, WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_CIE_XY_OUT_OF_RANGE,
			    "invalid %s", gamut_name);
		return;
	}

	/*
	 * That is not sufficient. There are points inside the triangle that
	 * would not be valid white points. But for now that's good enough.
	 */
	if (!is_point_inside_triangle(gamut->white_point.x,
				      gamut->white_point.y,
				      gamut->primary[0].x,
				      gamut->primary[0].y,
				      gamut->primary[1].x,
				      gamut->primary[1].y,
				      gamut->primary[2].x,
				      gamut->primary[2].y))
		store_error(builder, WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_CIE_XY_OUT_OF_RANGE,
			    "white point out of %s volume", gamut_name);
}

static void
validate_maxcll(struct weston_color_profile_param_builder *builder)
{
	if (!(builder->group_mask & WESTON_COLOR_PROFILE_PARAMS_LUMINANCE))
		return;

	if (builder->params.min_luminance >= builder->params.maxCLL)
		store_error(builder, WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_INCONSISTENT_LUMINANCES,
			    "maxCLL (%f) should be greater or equal to min luminance (%f)",
			    builder->params.maxCLL, builder->params.min_luminance);

	if (builder->params.max_luminance < builder->params.maxCLL)
		store_error(builder, WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_INCONSISTENT_LUMINANCES,
			    "maxCLL (%f) should not be greater than max luminance (%f)",
			    builder->params.maxCLL, builder->params.max_luminance);
}

static void
validate_maxfall(struct weston_color_profile_param_builder *builder)
{
	if (!(builder->group_mask & WESTON_COLOR_PROFILE_PARAMS_LUMINANCE))
		return;

	if (builder->params.min_luminance >= builder->params.maxFALL)
		store_error(builder, WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_INCONSISTENT_LUMINANCES,
			    "maxFALL (%f) should be greater or equal to min luminance (%f)",
			    builder->params.maxFALL, builder->params.min_luminance);

	if (builder->params.max_luminance < builder->params.maxFALL)
		store_error(builder, WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_INCONSISTENT_LUMINANCES,
			    "maxFALL (%f) should not be greater than max luminance (%f)",
			    builder->params.maxFALL, builder->params.max_luminance);
}

static void
builder_validate_params(struct weston_color_profile_param_builder *builder)
{
	if (builder->group_mask & WESTON_COLOR_PROFILE_PARAMS_PRIMARIES)
		validate_color_gamut(builder, &builder->params.primaries,
				     "primaries");

	if (builder->group_mask & WESTON_COLOR_PROFILE_PARAMS_TARGET_PRIMARIES)
		validate_color_gamut(builder, &builder->params.target_primaries,
				     "target primaries");

	if (builder->group_mask & WESTON_COLOR_PROFILE_PARAMS_MAXCLL)
		validate_maxcll(builder);

	if (builder->group_mask & WESTON_COLOR_PROFILE_PARAMS_MAXFALL)
		validate_maxfall(builder);
}

static void
builder_complete_params(struct weston_color_profile_param_builder *builder)
{
	/* If no target primaries were set, it matches the primaries. */
	if (!(builder->group_mask & WESTON_COLOR_PROFILE_PARAMS_TARGET_PRIMARIES))
		builder->params.target_primaries = builder->params.primaries;

	/*
	 * If luminance is not set, set it to negative. Same applies to maxCLL
	 * and maxFALL.
	 */

	if (!(builder->group_mask & WESTON_COLOR_PROFILE_PARAMS_LUMINANCE)) {
		builder->params.min_luminance = -1.0f;
		builder->params.max_luminance = -1.0f;
	}

	if (!(builder->group_mask & WESTON_COLOR_PROFILE_PARAMS_MAXCLL))
		builder->params.maxCLL = -1.0f;

	if (!(builder->group_mask & WESTON_COLOR_PROFILE_PARAMS_MAXFALL))
		builder->params.maxFALL = -1.0f;
}

/**
 * Creates a color profile from a struct weston_color_profile_param_builder
 * object.
 *
 * After creating the weston_color_profile_param_builder and setting the
 * appropriate parameters, this function should be called to finally create the
 * color profile. It checks if the parameters are consistent and, if so, call
 * the color manager to create the color profile.
 *
 * Also, this is a destructor function. It destroys the builder object.
 *
 * \param builder The object that has the parameters set.
 * \param name_part A string to be used in describing the profile.
 * \param err Set if there's an error, untouched otherwise. The first error code caught.
 * \param err_msg Set if there's an error, untouched otherwise. Must be free()'d
 * by the caller. Combination of all error messages caught. Not terminated with
 * a new line character.
 * \return The color profile created, or NULL on failure.
 */
struct weston_color_profile *
weston_color_profile_param_builder_create_color_profile(struct weston_color_profile_param_builder *builder,
							const char *name_part,
							enum weston_color_profile_param_builder_error *err,
							char **err_msg)
{
	struct weston_color_manager *cm = builder->compositor->color_manager;
	struct weston_color_profile_params *params = &builder->params;
	struct weston_color_profile *cprof = NULL;
	bool ret;

	/*
	 * See struct weston_color_profile_params description. That struct has
	 * some rules that we need to fullfil (e.g. target primaries must be
	 * set, even if client does not pass anything). In this function we
	 * complete the param set in order to fullfil such rules.
	 */
	builder_complete_params(builder);

	/* Ensure that params make sense together. */
	builder_validate_params_set(builder);

	/* Ensure that each param set is reasonable. */
	builder_validate_params(builder);

	/* Something went wrong, so error out. */
	if (builder->has_errors) {
		fflush(builder->err_fp);
		*err_msg = strdup(builder->err_msg);
		*err = builder->err;
		goto out;
	}

	ret = cm->get_color_profile_from_params(cm, params, name_part,
						&cprof, err_msg);
	if (!ret)
		*err = WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_UNSUPPORTED;

out:
	weston_color_profile_param_builder_destroy(builder);
	return cprof;
}
