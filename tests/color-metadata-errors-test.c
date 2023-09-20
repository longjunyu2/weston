/*
 * Copyright 2022 Collabora, Ltd.
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

#include <string.h>
#include <assert.h>
#include <math.h>

#include "weston-test-client-helper.h"
#include "weston-test-fixture-compositor.h"

#include "weston-private.h"
#include "libweston-internal.h"
#include "backend.h"
#include "color.h"
#include "id-number-allocator.h"
#include "shared/xalloc.h"

struct config_testcase {
	bool has_characteristics_key;
	const char *output_characteristics_name;
	const char *characteristics_name;
	const char *red_x;
	const char *green_y;
	const char *white_y;
	const char *min_L;
	int expected_retval;
	const char *expected_error;
};

static const struct config_testcase config_cases[] = {
	{
		false, "fred", "fred", "red_x=0.9", "green_y=0.8", "white_y=0.323", "min_L=1e-4", 0,
		""
	},
	{
		true, "fred", "fred", "red_x=0.9", "green_y= 0.8 ", "white_y=0.323", "min_L=1e-4", 0,
		""
	},
	{
		true, "fred", "fred", "red_x=0.9", "green_y= 0.8 ", "white_y=0.323", "", 0,
		""
	},
	{
		true, "notexisting", "fred", "red_x=0.9", "green_y=0.8", "white_y=0.323", "min_L=1e-4", -1,
		"Config error in weston.ini, output mockoutput: no [color_characteristics] section with 'name=notexisting' found.\n"
	},
	{
		true, "fr:ed", "fr:ed", "red_x=0.9", "green_y=0.8", "white_y=0.323", "min_L=1e-4", -1,
		"Config error in weston.ini [color_characteristics] name=fr:ed: reserved name. Do not use ':' character in the name.\n"
	},
	{
		true, "fred", "fred", "red_x=-5", "green_y=1.01", "white_y=0.323", "min_L=1e-4", -1,
		"Config error in weston.ini [color_characteristics] name=fred: red_x value -5.000000 is outside of the range 0.000000 - 1.000000.\n"
		"Config error in weston.ini [color_characteristics] name=fred: green_y value 1.010000 is outside of the range 0.000000 - 1.000000.\n"
	},
	{
		true, "fred", "fred", "red_x=haahaa", "green_y=-", "white_y=0.323", "min_L=1e-4", -1,
		"Config error in weston.ini [color_characteristics] name=fred: failed to parse the value of key red_x.\n"
		"Config error in weston.ini [color_characteristics] name=fred: failed to parse the value of key green_y.\n"
	},
	{
		true, "fred", "fred", "", "", "white_y=0.323", "min_L=1e-4", -1,
		"Config error in weston.ini [color_characteristics] name=fred: group 1 key red_x is missing. You must set either none or all keys of a group.\n"
		"Config error in weston.ini [color_characteristics] name=fred: group 1 key red_y is set. You must set either none or all keys of a group.\n"
		"Config error in weston.ini [color_characteristics] name=fred: group 1 key green_x is set. You must set either none or all keys of a group.\n"
		"Config error in weston.ini [color_characteristics] name=fred: group 1 key green_y is missing. You must set either none or all keys of a group.\n"
		"Config error in weston.ini [color_characteristics] name=fred: group 1 key blue_x is set. You must set either none or all keys of a group.\n"
		"Config error in weston.ini [color_characteristics] name=fred: group 1 key blue_y is set. You must set either none or all keys of a group.\n"
	},
	{
		true, "fred", "fred", "red_x=0.9", "green_y=0.8", "", "min_L=1e-4", -1,
		"Config error in weston.ini [color_characteristics] name=fred: group 2 key white_x is set. You must set either none or all keys of a group.\n"
		"Config error in weston.ini [color_characteristics] name=fred: group 2 key white_y is missing. You must set either none or all keys of a group.\n"
	},
};

static FILE *logfile;

static int
logger(const char *fmt, va_list arg)
{
	return vfprintf(logfile, fmt, arg);
}

static int
no_logger(const char *fmt, va_list arg)
{
	return 0;
}

static struct weston_config *
create_config(const struct config_testcase *t)
{
	struct compositor_setup setup;
	struct weston_config *wc;

	compositor_setup_defaults(&setup);
	weston_ini_setup(&setup,
			 cfgln("[output]"),
			 cfgln("name=mockoutput"),
			 t->has_characteristics_key ?
				cfgln("color_characteristics=%s", t->output_characteristics_name) :
				cfgln(""),
			 cfgln("eotf-mode=st2084"),

			 cfgln("[color_characteristics]"),
			 cfgln("name=%s", t->characteristics_name),
			 cfgln("maxFALL=1000"),
			 cfgln("%s", t->red_x),
			 cfgln("red_y=0.3"),
			 cfgln("blue_x=0.1"),
			 cfgln("blue_y=0.11"),
			 cfgln("green_x=0.1771"),
			 cfgln("%s", t->green_y),
			 cfgln("white_x=0.313"),
			 cfgln("%s", t->white_y),
			 cfgln("%s", t->min_L),
			 cfgln("max_L=65535.0"),

			 cfgln("[core]"),
			 cfgln("color-management=true"));

	wc = weston_config_parse(setup.config_file);
	free(setup.config_file);

	return wc;
}

struct mock_color_manager {
	struct weston_color_manager base;
	struct weston_hdr_metadata_type1 *test_hdr_meta;
};

static struct weston_output_color_outcome *
mock_create_output_color_outcome(struct weston_color_manager *cm_base,
				 struct weston_output *output)
{
	struct mock_color_manager *cm = container_of(cm_base, typeof(*cm), base);
	struct weston_output_color_outcome *co;

	co = xzalloc(sizeof *co);

	co->hdr_meta = *cm->test_hdr_meta;

	return co;
}

static struct weston_color_profile *
mock_cm_ref_stock_sRGB_color_profile(struct weston_color_manager *mock_cm)
{
	struct weston_color_profile *mock_cprof;

	mock_cprof = xzalloc(sizeof(*mock_cprof));

	mock_cprof->cm = mock_cm;
	mock_cprof->ref_count = 1;
	mock_cprof->description = xstrdup("mock cprof");
	mock_cprof->id = weston_idalloc_get_id(mock_cm->compositor->color_profile_id_generator);

	return mock_cprof;
}

static void
mock_cm_destroy_color_profile(struct weston_color_profile *mock_cprof)
{
	free(mock_cprof->description);
	free(mock_cprof);
}

/*
 * Manufacture various weston.ini and check what
 * wet_output_set_color_characteristics() says. Tests for the return value and
 * the error messages logged.
 */
TEST_P(color_characteristics_config_error, config_cases)
{
	const struct config_testcase *t = data;
	struct weston_config *wc;
	struct weston_config_section *section;
	int retval;
	char *logbuf;
	size_t logsize;
	struct mock_color_manager mock_cm = {
		.base.create_output_color_outcome = mock_create_output_color_outcome,
		.base.ref_stock_sRGB_color_profile = mock_cm_ref_stock_sRGB_color_profile,
		.base.destroy_color_profile = mock_cm_destroy_color_profile,
	};
	struct weston_compositor mock_compositor = {
		.color_manager = &mock_cm.base,
		.color_profile_id_generator = weston_idalloc_create(&mock_compositor),
	};
	struct weston_output mock_output = {};

	mock_cm.base.compositor = &mock_compositor;

	wl_list_init(&mock_compositor.plane_list);
	weston_output_init(&mock_output, &mock_compositor, "mockoutput");

	logfile = open_memstream(&logbuf, &logsize);
	weston_log_set_handler(logger, logger);

	wc = create_config(t);
	section = weston_config_get_section(wc, "output", "name", "mockoutput");
	assert(section);

	retval = wet_output_set_color_characteristics(&mock_output, wc, section);

	assert(fclose(logfile) == 0);
	logfile = NULL;

	testlog("retval %d, logs:\n%s\n", retval, logbuf);

	assert(retval == t->expected_retval);
	assert(strcmp(logbuf, t->expected_error) == 0);

	weston_config_destroy(wc);
	free(logbuf);
	weston_output_release(&mock_output);
	weston_idalloc_destroy(mock_compositor.color_profile_id_generator);
}

/* Setting NULL resets group_mask */
TEST(weston_output_set_color_characteristics_null)
{
	struct mock_color_manager mock_cm = {
		.base.create_output_color_outcome = mock_create_output_color_outcome,
		.base.ref_stock_sRGB_color_profile = mock_cm_ref_stock_sRGB_color_profile,
		.base.destroy_color_profile = mock_cm_destroy_color_profile,
	};
	struct weston_compositor mock_compositor = {
		.color_manager = &mock_cm.base,
		.color_profile_id_generator = weston_idalloc_create(&mock_compositor),
	};
	struct weston_output mock_output = {};

	mock_cm.base.compositor = &mock_compositor;

	wl_list_init(&mock_compositor.plane_list);
	weston_output_init(&mock_output, &mock_compositor, "mockoutput");

	mock_output.color_characteristics.group_mask = 1;
	weston_output_set_color_characteristics(&mock_output, NULL);
	assert(mock_output.color_characteristics.group_mask == 0);

	weston_output_release(&mock_output);
	weston_idalloc_destroy(mock_compositor.color_profile_id_generator);
}

struct value_testcase {
	unsigned field_index;
	float value;
	bool retval;
};

static const struct value_testcase value_cases[] = {
	{ 0, 0.0, true },
	{ 0, 1.0, true },
	{ 0, -0.001, false },
	{ 0, 1.01, false },
	{ 0, NAN, false },
	{ 0, HUGE_VALF, false },
	{ 0, -HUGE_VALF, false },
	{ 1, -1.0, false },
	{ 2, 2.0, false },
	{ 3, 2.0, false },
	{ 4, 2.0, false },
	{ 5, 2.0, false },
	{ 6, 2.0, false },
	{ 7, 2.0, false },
	{ 8, 0.99, false },
	{ 8, 65535.1, false },
	{ 9, 0.000099, false },
	{ 9, 6.55351, false },
	{ 10, 0.99, false },
	{ 10, 65535.1, false },
	{ 11, 0.99, false },
	{ 11, 65535.1, false },
};

/*
 * Modify one value in a known good metadata structure, and see how
 * validation reacts to it.
 */
TEST_P(hdr_metadata_type1_errors, value_cases)
{
	struct value_testcase *t = data;
	struct weston_hdr_metadata_type1 meta = {
		.group_mask = WESTON_HDR_METADATA_TYPE1_GROUP_ALL_MASK,
		.primary[0] = { 0.6650, 0.3261 },
		.primary[1] = { 0.2890, 0.6435 },
		.primary[2] = { 0.1491, 0.0507 },
		.white = { 0.3134, 0.3291 },
		.maxDML = 600.0,
		.minDML = 0.0001,
		.maxCLL = 600.0,
		.maxFALL = 400.0,
	};
	float *fields[] = {
		&meta.primary[0].x, &meta.primary[0].y,
		&meta.primary[1].x, &meta.primary[1].y,
		&meta.primary[2].x, &meta.primary[2].y,
		&meta.white.x, &meta.white.y,
		&meta.maxDML, &meta.minDML,
		&meta.maxCLL, &meta.maxFALL,
	};
	struct mock_color_manager mock_cm = {
		.base.create_output_color_outcome = mock_create_output_color_outcome,
		.base.ref_stock_sRGB_color_profile = mock_cm_ref_stock_sRGB_color_profile,
		.base.destroy_color_profile = mock_cm_destroy_color_profile,
		.test_hdr_meta = &meta,
	};
	struct weston_compositor mock_compositor = {
		.color_manager = &mock_cm.base,
		.color_profile_id_generator = weston_idalloc_create(&mock_compositor),
	};
	struct weston_output mock_output = {};
	bool ret;

	weston_log_set_handler(no_logger, no_logger);

	mock_cm.base.compositor = &mock_compositor;

	wl_list_init(&mock_compositor.plane_list);
	weston_output_init(&mock_output, &mock_compositor, "mockoutput");

	assert(t->field_index < ARRAY_LENGTH(fields));
	*fields[t->field_index] = t->value;
	ret = weston_output_set_color_outcome(&mock_output);
	assert(ret == t->retval);

	weston_output_color_outcome_destroy(&mock_output.color_outcome);
	weston_output_release(&mock_output);
	weston_idalloc_destroy(mock_compositor.color_profile_id_generator);
}

/* Unflagged members are ignored in validity check */
TEST(hdr_metadata_type1_ignore_unflagged)
{
	/* All values invalid, but also empty mask so none actually used. */
	struct weston_hdr_metadata_type1 meta = {
		.group_mask = 0,
		.primary[0] = { -1.0, -1.0 },
		.primary[1] = { -1.0, -1.0 },
		.primary[2] = { -1.0, -1.0 },
		.white = { -1.0, -1.0 },
		.maxDML = -1.0,
		.minDML = -1.0,
		.maxCLL = -1.0,
		.maxFALL = -1.0,
	};
	struct mock_color_manager mock_cm = {
		.base.create_output_color_outcome = mock_create_output_color_outcome,
		.base.ref_stock_sRGB_color_profile = mock_cm_ref_stock_sRGB_color_profile,
		.base.destroy_color_profile = mock_cm_destroy_color_profile,
		.test_hdr_meta = &meta,
	};
	struct weston_compositor mock_compositor = {
		.color_manager = &mock_cm.base,
		.color_profile_id_generator = weston_idalloc_create(&mock_compositor),
	};
	struct weston_output mock_output = {};
	bool ret;

	mock_cm.base.compositor = &mock_compositor;

	wl_list_init(&mock_compositor.plane_list);
	weston_log_set_handler(no_logger, no_logger);

	weston_output_init(&mock_output, &mock_compositor, "mockoutput");

	ret = weston_output_set_color_outcome(&mock_output);
	assert(ret);

	weston_output_color_outcome_destroy(&mock_output.color_outcome);
	weston_output_release(&mock_output);
	weston_idalloc_destroy(mock_compositor.color_profile_id_generator);
}

struct mode_testcase {
	bool color_management;
	uint32_t supported_eotf_mask;
	uint32_t supported_colorimetry_mask;
	const char *eotf_mode;
	const char *colorimetry_mode;
	enum weston_eotf_mode expected_eotf_mode;
	enum weston_colorimetry_mode expected_colorimetry_mode;
	int expected_retval;
	const char *expected_error;
};

static const struct mode_testcase mode_config_cases[] = {
	/* Defaults */
	{
		false, WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_DEFAULT, NULL, NULL,
		WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_DEFAULT,
		0, ""
	},
	/* Color management off, EOTF modes */
	{
		false, WESTON_EOTF_MODE_ALL_MASK, WESTON_COLORIMETRY_MODE_DEFAULT, "sdr", NULL,
		WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_DEFAULT,
		0, ""
	},
	{
		false, WESTON_EOTF_MODE_ALL_MASK, WESTON_COLORIMETRY_MODE_DEFAULT, "hdr-gamma", NULL,
		WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_DEFAULT,
		-1, "Error: EOTF mode hdr-gamma on output 'mockoutput' requires color-management=true in weston.ini\n"
	},
	{
		false, WESTON_EOTF_MODE_ALL_MASK, WESTON_COLORIMETRY_MODE_DEFAULT, "st2084", NULL,
		WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_DEFAULT,
		-1, "Error: EOTF mode st2084 on output 'mockoutput' requires color-management=true in weston.ini\n"
	},
	{
		false, WESTON_EOTF_MODE_ALL_MASK, WESTON_COLORIMETRY_MODE_DEFAULT, "hlg", NULL,
		WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_DEFAULT,
		-1, "Error: EOTF mode hlg on output 'mockoutput' requires color-management=true in weston.ini\n"
	},
	{
		false, WESTON_EOTF_MODE_ALL_MASK, WESTON_COLORIMETRY_MODE_DEFAULT, "nonosense", NULL,
		WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_DEFAULT,
		-1, "Error in config for output 'mockoutput': 'nonosense' is not a valid EOTF mode. Try one of: sdr hdr-gamma st2084 hlg\n"
	},
	/* Color management on, EOTF modes */
	{
		true, WESTON_EOTF_MODE_ALL_MASK, WESTON_COLORIMETRY_MODE_DEFAULT, "sdr", NULL,
		WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_DEFAULT,
		0, ""
	},
	{
		true, WESTON_EOTF_MODE_ALL_MASK, WESTON_COLORIMETRY_MODE_DEFAULT, "hdr-gamma", NULL,
		WESTON_EOTF_MODE_TRADITIONAL_HDR, WESTON_COLORIMETRY_MODE_DEFAULT,
		0, ""
	},
	{
		true, WESTON_EOTF_MODE_ALL_MASK, WESTON_COLORIMETRY_MODE_DEFAULT, "st2084", NULL,
		WESTON_EOTF_MODE_ST2084, WESTON_COLORIMETRY_MODE_DEFAULT,
		0, ""
	},
	{
		true, WESTON_EOTF_MODE_ALL_MASK, WESTON_COLORIMETRY_MODE_DEFAULT, "hlg", NULL,
		WESTON_EOTF_MODE_HLG, WESTON_COLORIMETRY_MODE_DEFAULT,
		0, ""
	},
	{
		true, WESTON_EOTF_MODE_ALL_MASK, WESTON_COLORIMETRY_MODE_DEFAULT, "nonosense", NULL,
		WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_DEFAULT,
		-1, "Error in config for output 'mockoutput': 'nonosense' is not a valid EOTF mode. Try one of: sdr hdr-gamma st2084 hlg\n"
	},
	/* unsupported EOTF mode */
	{
		true,
		WESTON_EOTF_MODE_SDR | WESTON_EOTF_MODE_TRADITIONAL_HDR | WESTON_EOTF_MODE_ST2084,
		WESTON_COLORIMETRY_MODE_DEFAULT, "hlg", NULL,
		WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_DEFAULT,
		-1, "Error: output 'mockoutput' does not support EOTF mode hlg.\n"
	},
	/* Color management off, colorimetry modes */
	{
		false, WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_ALL_MASK, NULL, "default",
		WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_DEFAULT,
		0, ""
	},
	{
		false, WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_ALL_MASK, NULL, "bt2020cycc",
		WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_DEFAULT,
		-1, "Error: Colorimetry mode bt2020cycc on output 'mockoutput' requires color-management=true in weston.ini\n"
	},
	{
		false, WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_ALL_MASK, NULL, "bt2020ycc",
		WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_DEFAULT,
		-1, "Error: Colorimetry mode bt2020ycc on output 'mockoutput' requires color-management=true in weston.ini\n"
	},
	{
		false, WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_ALL_MASK, NULL, "bt2020rgb",
		WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_DEFAULT,
		-1, "Error: Colorimetry mode bt2020rgb on output 'mockoutput' requires color-management=true in weston.ini\n"
	},
	{
		false, WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_ALL_MASK, NULL, "p3d65",
		WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_DEFAULT,
		-1, "Error: Colorimetry mode p3d65 on output 'mockoutput' requires color-management=true in weston.ini\n"
	},
	{
		false, WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_ALL_MASK, NULL, "p3dci",
		WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_DEFAULT,
		-1, "Error: Colorimetry mode p3dci on output 'mockoutput' requires color-management=true in weston.ini\n"
	},
	{
		false, WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_ALL_MASK, NULL, "ictcp",
		WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_DEFAULT,
		-1, "Error: Colorimetry mode ictcp on output 'mockoutput' requires color-management=true in weston.ini\n"
	},
	{
		false, WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_ALL_MASK, NULL, "imagine that",
		WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_DEFAULT,
		-1, "Error in config for output 'mockoutput': 'imagine that' is not a valid colorimetry mode. Try one of: default bt2020cycc bt2020ycc bt2020rgb p3d65 p3dci ictcp\n"
	},
	/* Color management on, colorimetry modes */
	{
		true, WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_ALL_MASK, NULL, "default",
		WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_DEFAULT,
		0, ""
	},
	{
		true, WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_ALL_MASK, NULL, "bt2020cycc",
		WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_BT2020_CYCC,
		0, ""
	},
	{
		true, WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_ALL_MASK, NULL, "bt2020ycc",
		WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_BT2020_YCC,
		0, ""
	},
	{
		true, WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_ALL_MASK, NULL, "bt2020rgb",
		WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_BT2020_RGB,
		0, ""
	},
	{
		true, WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_ALL_MASK, NULL, "p3d65",
		WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_P3D65,
		0, ""
	},
	{
		true, WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_ALL_MASK, NULL, "p3dci",
		WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_P3DCI,
		0, ""
	},
	{
		true, WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_ALL_MASK, NULL, "ictcp",
		WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_ICTCP,
		0, ""
	},
	{
		true, WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_ALL_MASK, NULL, "imagine that",
		WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_DEFAULT,
		-1, "Error in config for output 'mockoutput': 'imagine that' is not a valid colorimetry mode. Try one of: default bt2020cycc bt2020ycc bt2020rgb p3d65 p3dci ictcp\n"
	},
	/* Unsupported colorimetry mode */
	{
		true, WESTON_EOTF_MODE_SDR,
		WESTON_COLORIMETRY_MODE_DEFAULT | WESTON_COLORIMETRY_MODE_BT2020_RGB | WESTON_COLORIMETRY_MODE_BT2020_CYCC | WESTON_COLORIMETRY_MODE_P3D65,
		NULL, "ictcp",
		WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_DEFAULT,
		-1, "Error: output 'mockoutput' does not support colorimetry mode ictcp.\n"
	},
};

static struct weston_config *
create_mode_config(const struct mode_testcase *t)
{
	struct compositor_setup setup;
	struct weston_config *wc;

	compositor_setup_defaults(&setup);
	weston_ini_setup(&setup,
			 cfgln("[output]"),
			 cfgln("name=mockoutput"),

			 t->eotf_mode ?
				cfgln("eotf-mode=%s", t->eotf_mode) :
				cfgln(""),

			 t->colorimetry_mode ?
				cfgln("colorimetry-mode=%s", t->colorimetry_mode) :
				cfgln("")
			);

	wc = weston_config_parse(setup.config_file);
	free(setup.config_file);

	return wc;
}

/*
 * Manufacture various weston.ini and check what
 * wet_output_set_eotf_mode() and wet_output_set_colorimetry_mode() says.
 * Tests for the return value and the error messages logged.
 */
TEST_P(mode_config_error, mode_config_cases)
{
	const struct mode_testcase *t = data;
	struct mock_color_manager mock_cm = {
		.base.create_output_color_outcome = mock_create_output_color_outcome,
		.base.ref_stock_sRGB_color_profile = mock_cm_ref_stock_sRGB_color_profile,
		.base.destroy_color_profile = mock_cm_destroy_color_profile,
	};
	struct weston_compositor mock_compositor = {
		.color_manager = &mock_cm.base,
		.color_profile_id_generator = weston_idalloc_create(&mock_compositor),
	};

	struct weston_config *wc;
	struct weston_config_section *section;
	int retval;
	int attached;
	char *logbuf;
	size_t logsize;
	struct weston_head mock_head = {};
	struct weston_output mock_output = {};

	mock_cm.base.compositor = &mock_compositor;

	wl_list_init(&mock_compositor.plane_list);

	weston_output_init(&mock_output, &mock_compositor, "mockoutput");
	weston_head_init(&mock_head, "mockhead");
	weston_head_set_supported_eotf_mask(&mock_head, t->supported_eotf_mask);
	weston_head_set_supported_colorimetry_mask(&mock_head, t->supported_colorimetry_mask);
	attached = weston_output_attach_head(&mock_output, &mock_head);
	assert(attached == 0);

	logfile = open_memstream(&logbuf, &logsize);
	weston_log_set_handler(logger, logger);

	wc = create_mode_config(t);
	section = weston_config_get_section(wc, "output", "name", "mockoutput");
	assert(section);

	retval = wet_output_set_eotf_mode(&mock_output, section, t->color_management);
	if (retval == 0) {
		retval = wet_output_set_colorimetry_mode(&mock_output, section,
							 t->color_management);
	}

	assert(fclose(logfile) == 0);
	logfile = NULL;

	testlog("retval %d, logs:\n%s\n", retval, logbuf);

	assert(retval == t->expected_retval);
	assert(strcmp(logbuf, t->expected_error) == 0);
	assert(weston_output_get_eotf_mode(&mock_output) == t->expected_eotf_mode);
	assert(weston_output_get_colorimetry_mode(&mock_output) == t->expected_colorimetry_mode);

	weston_config_destroy(wc);
	free(logbuf);
	weston_output_release(&mock_output);
	weston_head_release(&mock_head);
	weston_idalloc_destroy(mock_compositor.color_profile_id_generator);
}
