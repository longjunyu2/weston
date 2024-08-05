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

#include "config.h"

#ifdef HAVE_LCMS
#include <lcms2.h>
#define LCMS_INTENT(x) .lcms_intent = (x)
#else
/* invalid value */
#define LCMS_INTENT(x) .lcms_intent = 0xffffffff
#endif

#include <libweston/libweston.h>
#include <color-properties.h>
#include "shared/helpers.h"
#include "shared/weston-assert.h"

#include "color-management-v1-server-protocol.h"

enum tf_has_parameters {
        YES_PARAMETERS = true,
        NO_PARAMETERS = false,
};

static const struct weston_color_feature_info color_feature_info_table[] = {
	{
		.feature = WESTON_COLOR_FEATURE_ICC,
		.desc = "Allow clients to use the new_icc_creator request " \
			"from the CM&HDR protocol extension",
		.protocol_feature = XX_COLOR_MANAGER_V4_FEATURE_ICC_V2_V4,
	},
	{
		.feature = WESTON_COLOR_FEATURE_PARAMETRIC,
		.desc = "Allow clients to use the new_parametric_creator " \
			"request from the CM&HDR protocol extension",
		.protocol_feature = XX_COLOR_MANAGER_V4_FEATURE_PARAMETRIC,
	},
	{
		.feature = WESTON_COLOR_FEATURE_SET_PRIMARIES,
		.desc = "Allow clients to use the parametric set_primaries " \
			"request from the CM&HDR protocol extension",
		.protocol_feature = XX_COLOR_MANAGER_V4_FEATURE_SET_PRIMARIES,
	},
	{
		.feature = WESTON_COLOR_FEATURE_SET_TF_POWER,
		.desc = "Allow clients to use the parametric set_tf_power " \
			"request from the CM&HDR protocol extension",
		.protocol_feature = XX_COLOR_MANAGER_V4_FEATURE_SET_TF_POWER,
	},
	{
		.feature = WESTON_COLOR_FEATURE_SET_MASTERING_DISPLAY_PRIMARIES,
		.desc = "Allow clients to use the parametric " \
			"set_mastering_display_primaries request from the " \
			"CM&HDR protocol extension",
		.protocol_feature = XX_COLOR_MANAGER_V4_FEATURE_SET_MASTERING_DISPLAY_PRIMARIES,
	},
	{
		.feature = WESTON_COLOR_FEATURE_EXTENDED_TARGET_VOLUME,
		.desc = "Allow clients to specify (through the CM&HDR protocol " \
			"extension) target color volumes that extend outside of the" \
			"primary color volume. This can only be supported when feature " \
			"WESTON_COLOR_FEATURE_SET_MASTERING_DISPLAY_PRIMARIES " \
			"is supported",
		.protocol_feature = XX_COLOR_MANAGER_V4_FEATURE_EXTENDED_TARGET_VOLUME,
	},
};

static const struct weston_render_intent_info render_intent_info_table[] = {
	{
		.intent = WESTON_RENDER_INTENT_PERCEPTUAL,
		.desc = "Perceptual",
		.protocol_intent = XX_COLOR_MANAGER_V4_RENDER_INTENT_PERCEPTUAL,
		LCMS_INTENT(INTENT_PERCEPTUAL),
		.bps = false,
	},
	{
		.intent = WESTON_RENDER_INTENT_RELATIVE,
		.desc = "Media-relative colorimetric",
		.protocol_intent = XX_COLOR_MANAGER_V4_RENDER_INTENT_RELATIVE,
		LCMS_INTENT(INTENT_RELATIVE_COLORIMETRIC),
		.bps = false,
	},
	{
		.intent = WESTON_RENDER_INTENT_SATURATION,
		.desc = "Saturation",
		.protocol_intent = XX_COLOR_MANAGER_V4_RENDER_INTENT_SATURATION,
		LCMS_INTENT(INTENT_SATURATION),
		.bps = false,
	},
	{
		.intent = WESTON_RENDER_INTENT_ABSOLUTE,
		.desc = "ICC-absolute colorimetric",
		.protocol_intent = XX_COLOR_MANAGER_V4_RENDER_INTENT_ABSOLUTE,
		LCMS_INTENT(INTENT_ABSOLUTE_COLORIMETRIC),
		.bps = false,
	},
	{
		.intent = WESTON_RENDER_INTENT_RELATIVE_BPC,
		.desc = "Media-relative colorimetric + black point compensation",
		.protocol_intent = XX_COLOR_MANAGER_V4_RENDER_INTENT_RELATIVE_BPC,
		LCMS_INTENT(INTENT_RELATIVE_COLORIMETRIC),
		.bps = true,
	},
};

static const struct weston_color_primaries_info color_primaries_info_table[] = {
	{
		.primaries = WESTON_PRIMARIES_CICP_SRGB,
		.desc = "Color primaries for the sRGB color space as defined by " \
			"the BT.709 standard",
		.protocol_primaries = XX_COLOR_MANAGER_V4_PRIMARIES_SRGB,
		.color_gamut = {
			.primary = { { 0.64, 0.33 }, /* RGB order */
				     { 0.30, 0.60 },
				     { 0.15, 0.06 },
			},
			.white_point = { 0.3127, 0.3290 },
		},
	},
	{
		.primaries = WESTON_PRIMARIES_CICP_PAL_M,
		.desc = "Color primaries for PAL-M as defined by the BT.470 standard",
		.protocol_primaries = XX_COLOR_MANAGER_V4_PRIMARIES_PAL_M,
		.color_gamut = {
			.primary = { { 0.67, 0.33 }, /* RGB order */
				     { 0.21, 0.71 },
				     { 0.14, 0.08 },
			},
			.white_point = { 0.3101, 0.3162 },
		},
	},
	{
		.primaries = WESTON_PRIMARIES_CICP_PAL,
		.desc = "Color primaries for PAL as defined by the BT.601 standard",
		.protocol_primaries = XX_COLOR_MANAGER_V4_PRIMARIES_PAL,
		.color_gamut = {
			.primary = { { 0.64, 0.33 }, /* RGB order */
				     { 0.29, 0.60 },
				     { 0.15, 0.06 },
			},
			.white_point = { 0.3127, 0.3290 },
		},
	},
	{
		.primaries = WESTON_PRIMARIES_CICP_NTSC,
		.desc = "Color primaries for NTSC as defined by the BT.601 standard",
		.protocol_primaries = XX_COLOR_MANAGER_V4_PRIMARIES_NTSC,
		.color_gamut = {
			.primary = { { 0.630, 0.340 }, /* RGB order */
				     { 0.310, 0.595 },
				     { 0.155, 0.070 },
			},
			.white_point = { 0.3127, 0.3290 },
		},
	},
	{
		.primaries = WESTON_PRIMARIES_CICP_GENERIC_FILM,
		.desc = "Generic film with color filters using Illuminant C",
		.protocol_primaries = XX_COLOR_MANAGER_V4_PRIMARIES_GENERIC_FILM,
		.color_gamut = {
			.primary = { { 0.681, 0.319 }, /* RGB order */
				     { 0.243, 0.692 },
				     { 0.145, 0.049 },
			},
			.white_point = { 0.3101, 0.3162 },
		},
	},
	{
		.primaries = WESTON_PRIMARIES_CICP_BT2020,
		.desc = "Color primaries as defined by the BT.2020 and BT.2100 " \
			"standard",
		.protocol_primaries = XX_COLOR_MANAGER_V4_PRIMARIES_BT2020,
		.color_gamut = {
			.primary = { { 0.708, 0.292 }, /* RGB order */
				     { 0.170, 0.797 },
				     { 0.131, 0.046 },
			},
			.white_point = { 0.3127, 0.3290 },
		},
	},
	{
		.primaries = WESTON_PRIMARIES_CICP_CIE1931_XYZ,
		.desc = "Color primaries of the full CIE 1931 XYZ color space",
		.protocol_primaries = XX_COLOR_MANAGER_V4_PRIMARIES_CIE1931_XYZ,
		.color_gamut = {
			.primary = { { 1.0, 0.0 }, /* RGB order */
				     { 0.0, 1.0 },
				     { 0.0, 0.0 },
			},
			.white_point = { 0.3333, 0.3333 },
		},
	},
	{
		.primaries = WESTON_PRIMARIES_CICP_DCI_P3,
		.desc = "Color primaries of the DCI P3 color space as defined by " \
			"the SMPTE RP 431 standard",
		.protocol_primaries = XX_COLOR_MANAGER_V4_PRIMARIES_DCI_P3,
		.color_gamut = {
			.primary = { { 0.680, 0.320 }, /* RGB order */
				     { 0.265, 0.690 },
				     { 0.150, 0.060 },
			},
			.white_point = { 0.314, 0.351 },
		},
	},
	{
		.primaries = WESTON_PRIMARIES_CICP_DISPLAY_P3,
		.desc = "Color primaries of Display P3 variant of the DCI-P3 color " \
			"space as defined by the SMPTE EG 432 standard",
		.protocol_primaries = XX_COLOR_MANAGER_V4_PRIMARIES_DISPLAY_P3,
		.color_gamut = {
			.primary = { { 0.680, 0.320 }, /* RGB order */
				     { 0.265, 0.690 },
				     { 0.150, 0.060 },
			},
			.white_point = { 0.3127, 0.3290 },
		},
	},
	{
		.primaries = WESTON_PRIMARIES_ADOBE_RGB,
		.desc = "Color primaries of the Adobe RGB color space as defined " \
			"by the ISO 12640 standard",
		.protocol_primaries = XX_COLOR_MANAGER_V4_PRIMARIES_ADOBE_RGB,
		.color_gamut = {
			.primary = { { 0.64, 0.33 }, /* RGB order */
				     { 0.21, 0.71 },
				     { 0.15, 0.06 },
			},
			.white_point = { 0.3127, 0.3290 },
		},
	},
};

static const struct weston_color_tf_info color_tf_info_table[] = {
	{
		.tf = WESTON_TF_LINEAR,
		.desc = "Linear transfer function",
		.protocol_tf = XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_LINEAR,
		.has_parameters = NO_PARAMETERS,
	},
	{
		.tf = WESTON_TF_GAMMA22,
		.desc = "Assumed display gamma 2.2 transfer function",
		.protocol_tf = XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_GAMMA22,
		.has_parameters = NO_PARAMETERS,
	},
	{
		.tf = WESTON_TF_GAMMA28,
		.desc = "Assumed display gamma 2.8 transfer function",
		.protocol_tf = XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_GAMMA28,
		.has_parameters = NO_PARAMETERS,
	},
	{
		.tf = WESTON_TF_SRGB,
		.desc = "sRGB piece-wise transfer function",
		.protocol_tf = XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_SRGB,
		.has_parameters = NO_PARAMETERS,
	},
	{
		.tf = WESTON_TF_EXT_SRGB,
		.desc = "Extended sRGB piece-wise transfer function",
		.protocol_tf = XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_EXT_SRGB,
		.has_parameters = NO_PARAMETERS,
	},
	{
		.tf = WESTON_TF_BT709,
		.desc = "BT.709 transfer function",
		.protocol_tf = XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_BT709,
		.has_parameters = NO_PARAMETERS,
	},
	{
		.tf = WESTON_TF_BT1361,
		.desc = "BT.1361 extended transfer function",
		.protocol_tf = XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_BT1361,
		.has_parameters = NO_PARAMETERS,
	},
	{
		.tf = WESTON_TF_ST240,
		.desc = "SMPTE ST 240 transfer function",
		.protocol_tf = XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_ST240,
		.has_parameters = NO_PARAMETERS,
	},
	{
		.tf = WESTON_TF_ST428,
		.desc = "SMPTE ST 428 transfer function",
		.protocol_tf = XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_ST428,
		.has_parameters = NO_PARAMETERS,
	},
	{
		.tf = WESTON_TF_ST2084_PQ,
		.desc = "Perceptual quantizer transfer function",
		.protocol_tf = XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_ST2084_PQ,
		.has_parameters = NO_PARAMETERS,
	},
	{
		.tf = WESTON_TF_LOG_100,
		.desc = "Logarithmic 100:1 transfer function",
		.protocol_tf = XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_LOG_100,
		.has_parameters = NO_PARAMETERS,
	},
	{
		.tf = WESTON_TF_LOG_316,
		.desc = "Logarithmic (100*Sqrt(10) : 1) transfer function",
		.protocol_tf = XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_LOG_316,
		.has_parameters = NO_PARAMETERS,
	},
	{
		.tf = WESTON_TF_XVYCC,
		.desc = "IEC 61966-2-4 transfer function",
		.protocol_tf = XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_XVYCC,
		.has_parameters = NO_PARAMETERS,
	},
	{
		.tf = WESTON_TF_HLG,
		.desc = "Hybrid log-gamma transfer function",
		.protocol_tf = XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_HLG,
		.has_parameters = NO_PARAMETERS,
	},
        {
		.tf = WESTON_TF_POWER,
		.desc = "Parameterized power-law transfer function",
		.has_parameters = YES_PARAMETERS,
        },
};

WL_EXPORT const struct weston_color_feature_info *
weston_color_feature_info_from(struct weston_compositor *compositor,
			       enum weston_color_feature feature)
{
	unsigned int i;

	for (i = 0; i < ARRAY_LENGTH(color_feature_info_table); i++)
		if (color_feature_info_table[i].feature == feature)
			return &color_feature_info_table[i];

	weston_assert_not_reached(compositor, "unknown color feature");
}

WL_EXPORT const struct weston_render_intent_info *
weston_render_intent_info_from(struct weston_compositor *compositor,
			       enum weston_render_intent intent)
{
	unsigned int i;

	for (i = 0; i < ARRAY_LENGTH(render_intent_info_table); i++)
		if (render_intent_info_table[i].intent == intent)
			return &render_intent_info_table[i];

	weston_assert_not_reached(compositor, "unknown render intent");
}

WL_EXPORT const struct weston_render_intent_info *
weston_render_intent_info_from_protocol(struct weston_compositor *compositor,
					uint32_t protocol_intent)
{
	unsigned int i;

	for (i = 0; i < ARRAY_LENGTH(render_intent_info_table); i++)
		if (render_intent_info_table[i].protocol_intent == protocol_intent)
			return &render_intent_info_table[i];

	return NULL;
}

WL_EXPORT const struct weston_color_primaries_info *
weston_color_primaries_info_from(struct weston_compositor *compositor,
				 enum weston_color_primaries primaries)
{
	unsigned int i;

	for (i = 0; i < ARRAY_LENGTH(color_primaries_info_table); i++)
		if (color_primaries_info_table[i].primaries == primaries)
			return &color_primaries_info_table[i];

	weston_assert_not_reached(compositor, "unknown primaries");
}

WL_EXPORT const struct weston_color_tf_info *
weston_color_tf_info_from(struct weston_compositor *compositor,
			  enum weston_transfer_function tf)
{
	unsigned int i;

	for (i = 0; i < ARRAY_LENGTH(color_tf_info_table); i++)
		if (color_tf_info_table[i].tf == tf)
			return &color_tf_info_table[i];

	weston_assert_not_reached(compositor, "unknown tf");
}
