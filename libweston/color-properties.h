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

#ifndef WESTON_COLOR_CHARACTERISTICS_H
#define WESTON_COLOR_CHARACTERISTICS_H

#include <stdint.h>
#include <stdbool.h>

#include <libweston/libweston.h>

struct weston_compositor;

/**
 * Color features.
 */
enum weston_color_feature {
	WESTON_COLOR_FEATURE_ICC = 0,
	WESTON_COLOR_FEATURE_PARAMETRIC,
	WESTON_COLOR_FEATURE_SET_PRIMARIES,
	WESTON_COLOR_FEATURE_SET_TF_POWER,
	WESTON_COLOR_FEATURE_SET_MASTERING_DISPLAY_PRIMARIES,
	WESTON_COLOR_FEATURE_EXTENDED_TARGET_VOLUME,
};

/**
 * Rendering intents.
 */
enum weston_render_intent {
	WESTON_RENDER_INTENT_PERCEPTUAL = 0,
	WESTON_RENDER_INTENT_RELATIVE,
	WESTON_RENDER_INTENT_SATURATION,
	WESTON_RENDER_INTENT_ABSOLUTE,
	WESTON_RENDER_INTENT_RELATIVE_BPC,
};

struct weston_color_feature_info {
	/** Our internal representation for the features. */
	enum weston_color_feature feature;

	/** String describing the feature. */
	const char *desc;

	/** CM&HDR protocol extension value representing the feature. */
	uint32_t protocol_feature;
};

struct weston_render_intent_info {
	/** Our internal representation of the intents. */
	enum weston_render_intent intent;

	/* String describing the intent. */
	const char *desc;

	/** CM&HDR protocol extension value representing the intent. */
	uint32_t protocol_intent;

	/** LittleCMS representation. */
	uint32_t lcms_intent;

	/** Black-point compensation? */
	bool bps;
};

struct weston_color_primaries_info {
        /** Our internal representation for the primaries. */
        enum weston_color_primaries primaries;

	/** Raw values for the primaries. */
	struct weston_color_gamut color_gamut;

        /** String describing the primaries. */
        const char *desc;

        /** CM&HDR protocol extension value representing the primaries. */
        uint32_t protocol_primaries;
};

struct weston_color_tf_info {
        /** Our internal representation for the tf. */
        enum weston_transfer_function tf;

        /** String describing the tf. */
        const char *desc;

        /** CM&HDR protocol extension value representing the tf. */
        uint32_t protocol_tf;

	/* The protocol also has support for parameterized functions, i.e.
	 * certain known functions that clients can define passing arbitrary
	 * parameters. */
	bool has_parameters;
};

const struct weston_color_feature_info *
weston_color_feature_info_from(struct weston_compositor *compositor,
			       enum weston_color_feature feature);

const struct weston_render_intent_info *
weston_render_intent_info_from(struct weston_compositor *compositor,
			       enum weston_render_intent intent);

const struct weston_render_intent_info *
weston_render_intent_info_from_protocol(struct weston_compositor *compositor,
                                        uint32_t protocol_intent);

const struct weston_color_primaries_info *
weston_color_primaries_info_from(struct weston_compositor *compositor,
                                 enum weston_color_primaries primaries);

const struct weston_color_tf_info *
weston_color_tf_info_from(struct weston_compositor *compositor,
			  enum weston_transfer_function tf);

#endif /* WESTON_COLOR_CHARACTERISTICS_H */
