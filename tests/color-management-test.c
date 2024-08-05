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

#include "color-properties.h"
#include "weston-test-client-helper.h"
#include "weston-test-fixture-compositor.h"
#include "shared/xalloc.h"
#include "lcms_util.h"

#include "color-management-v1-client-protocol.h"

#include <fcntl.h>
#include <sys/stat.h>

static char srgb_icc_profile_path[500] = "\0";

enum image_descr_info_event {
	IMAGE_DESCR_INFO_EVENT_ICC_FD = 1,
	IMAGE_DESCR_INFO_EVENT_PRIMARIES_NAMED,
	IMAGE_DESCR_INFO_EVENT_PRIMARIES,
	IMAGE_DESCR_INFO_EVENT_TF_NAMED,
	IMAGE_DESCR_INFO_EVENT_TF_POWER_EXP,
	IMAGE_DESCR_INFO_EVENT_TARGET_PRIMARIES,
	IMAGE_DESCR_INFO_EVENT_TARGET_MAXCLL,
	IMAGE_DESCR_INFO_EVENT_TARGET_MAXFALL,
	IMAGE_DESCR_INFO_EVENT_TARGET_LUMINANCE,
};

const struct lcms_pipeline pipeline_sRGB = {
	.color_space = "sRGB",
	.prim_output = {
		.Red =   { 0.640, 0.330, 1.0 },
		.Green = { 0.300, 0.600, 1.0 },
		.Blue =  { 0.150, 0.060, 1.0 }
	},
	.pre_fn = TRANSFER_FN_SRGB_EOTF,
	.mat = LCMSMAT3(1.0, 0.0, 0.0,
			0.0, 1.0, 0.0,
			0.0, 0.0, 1.0),
	.post_fn = TRANSFER_FN_SRGB_EOTF_INVERSE
};

struct image_description {
	struct xx_image_description_v4 *xx_image_descr;

	enum image_description_status {
		CM_IMAGE_DESC_NOT_CREATED = 0,
		CM_IMAGE_DESC_READY,
		CM_IMAGE_DESC_FAILED,
	} status;

	/* color_manager::image_descr_list */
	struct wl_list link;

	/* For ICC-based image descriptions. */
	int32_t icc_fd;
	uint32_t icc_size;

	/* For parametric images descriptions. */
	enum xx_color_manager_v4_primaries primaries_named;
	struct weston_color_gamut primaries;
	enum xx_color_manager_v4_transfer_function tf_named;
	float tf_power;
	struct weston_color_gamut target_primaries;
	float target_min_lum, target_max_lum;
	float target_max_cll;
	float target_max_fall;
};

struct image_description_info {
	struct xx_image_description_info_v4 *xx_image_description_info;
	struct image_description *image_descr;

	/* Bitfield that holds what events the compositor has sent us through
	 * the image_descr_info object. For each event image_descr_info_event v
	 * received, the bit v of this bitfield will be set to 1. */
	uint32_t events_received;
};

struct color_manager {
        struct xx_color_manager_v4 *manager;

	struct xx_color_management_output_v4 *output;
	struct xx_color_management_surface_v4 *surface;
	struct xx_color_management_feedback_surface_v4 *feedback_surface;

	struct wl_list image_descr_list; /* image_description::link */

	/* Bitfield that holds what color features are supported. If enum
	 * supported_color_feature v is supported, bit v will be set to 1. */
	uint32_t supported_features;

	/* Bitfield that holds what rendering intents are supported. If enum
	 * supported_render_intent v is supported, bit v will be set to 1. */
	uint32_t supported_rendering_intents;
};

static struct image_description *
image_description_create(void)
{
	struct image_description *image_descr = xzalloc(sizeof(*image_descr));

	return image_descr;
}

static void
image_description_destroy(struct image_description *image_descr)
{
	wl_list_remove(&image_descr->link);
	xx_image_description_v4_destroy(image_descr->xx_image_descr);
	free(image_descr);
}

static void
image_descr_ready(void *data, struct xx_image_description_v4 *xx_image_description_v4,
		  uint32_t identity)
{
	struct image_description *image_descr = data;

	image_descr->status = CM_IMAGE_DESC_READY;
}

static void
image_descr_failed(void *data, struct xx_image_description_v4 *xx_image_description_v4,
		   uint32_t cause, const char *msg)
{
	struct image_description *image_descr = data;

	image_descr->status = CM_IMAGE_DESC_FAILED;

	testlog("Failed to create image description:\n" \
		"    cause: %u, msg: %s\n", cause, msg);
}

static const struct xx_image_description_v4_listener
image_descr_iface = {
	.ready = image_descr_ready,
	.failed = image_descr_failed,
};

static void
image_descr_info_received(struct image_description_info *image_descr_info,
			  enum image_descr_info_event ev)
{
	/* TODO: replace this assert with weston_assert_uint32_mask_bit_is_clear
	 * when we start using weston-assert in the test suite. */
	assert(!((image_descr_info->events_received >> ev) & 1));
	image_descr_info->events_received |= (1 << ev);
}

static void
image_descr_info_primaries(void *data,
			   struct xx_image_description_info_v4 *xx_image_description_info_v4,
			   int32_t r_x, int32_t r_y, int32_t g_x, int32_t g_y,
			   int32_t b_x, int32_t b_y, int32_t w_x, int32_t w_y)
{
	struct image_description_info *image_descr_info = data;
	struct image_description *image_descr = image_descr_info->image_descr;


	image_descr_info_received(image_descr_info,
				  IMAGE_DESCR_INFO_EVENT_PRIMARIES);

	image_descr->primaries.primary[0].x = r_x / 10000.0;
	image_descr->primaries.primary[0].y = r_y / 10000.0;
	image_descr->primaries.primary[1].x = g_x / 10000.0;
	image_descr->primaries.primary[1].y = g_y / 10000.0;
	image_descr->primaries.primary[2].x = b_x / 10000.0;
	image_descr->primaries.primary[2].y = b_y / 10000.0;
	image_descr->primaries.white_point.x = w_x / 10000.0;
	image_descr->primaries.white_point.y = w_y / 10000.0;
}

static void
image_descr_info_primaries_named(void *data,
				 struct xx_image_description_info_v4 *xx_image_description_info_v4,
				 uint32_t primaries)
{
	struct image_description_info *image_descr_info = data;
	struct image_description *image_descr = image_descr_info->image_descr;

	image_descr_info_received(image_descr_info,
				  IMAGE_DESCR_INFO_EVENT_PRIMARIES_NAMED);

	image_descr->primaries_named = primaries;
}

static void
image_descr_info_tf_named(void *data,
			  struct xx_image_description_info_v4 *xx_image_description_info_v4,
			  uint32_t tf)
{
	struct image_description_info *image_descr_info = data;
	struct image_description *image_descr = image_descr_info->image_descr;

	image_descr_info_received(image_descr_info,
				  IMAGE_DESCR_INFO_EVENT_TF_NAMED);

	image_descr->tf_named = tf;
}

static void
image_descr_info_tf_power(void *data,
			  struct xx_image_description_info_v4 *xx_image_description_info_v4,
			  uint32_t tf_power)
{
	struct image_description_info *image_descr_info = data;
	struct image_description *image_descr = image_descr_info->image_descr;

	image_descr_info_received(image_descr_info,
				  IMAGE_DESCR_INFO_EVENT_TF_POWER_EXP);

	image_descr->tf_power = tf_power / 10000.0;
}

static void
image_descr_info_target_primaries(void *data,
				  struct xx_image_description_info_v4 *xx_image_description_info_v4,
				  int32_t r_x, int32_t r_y, int32_t g_x, int32_t g_y,
				  int32_t b_x, int32_t b_y, int32_t w_x, int32_t w_y)
{
	struct image_description_info *image_descr_info = data;
	struct image_description *image_descr = image_descr_info->image_descr;

	image_descr_info_received(image_descr_info,
				  IMAGE_DESCR_INFO_EVENT_TARGET_PRIMARIES);

	image_descr->target_primaries.primary[0].x = r_x / 10000.0;
	image_descr->target_primaries.primary[0].y = r_y / 10000.0;
	image_descr->target_primaries.primary[1].x = g_x / 10000.0;
	image_descr->target_primaries.primary[1].y = g_y / 10000.0;
	image_descr->target_primaries.primary[2].x = b_x / 10000.0;
	image_descr->target_primaries.primary[2].y = b_y / 10000.0;
	image_descr->target_primaries.white_point.x = w_x / 10000.0;
	image_descr->target_primaries.white_point.y = w_y / 10000.0;
}

static void
image_descr_info_target_luminance(void *data,
				  struct xx_image_description_info_v4 *xx_image_description_info_v4,
				  uint32_t min_lum, uint32_t max_lum)
{
	struct image_description_info *image_descr_info = data;
	struct image_description *image_descr = image_descr_info->image_descr;

	image_descr_info_received(image_descr_info,
				  IMAGE_DESCR_INFO_EVENT_TARGET_LUMINANCE);

	image_descr->target_min_lum = min_lum / 10000.0;
	image_descr->target_max_lum = max_lum;
}

static void
image_descr_info_target_max_cll(void *data,
				struct xx_image_description_info_v4 *xx_image_description_info_v4,
				uint32_t maxCLL)
{
	struct image_description_info *image_descr_info = data;
	struct image_description *image_descr = image_descr_info->image_descr;

	image_descr_info_received(image_descr_info,
				  IMAGE_DESCR_INFO_EVENT_TARGET_MAXCLL);

	image_descr->target_max_cll = maxCLL;
}

static void
image_descr_info_target_max_fall(void *data,
				 struct xx_image_description_info_v4 *xx_image_description_info_v4,
				 uint32_t maxFALL)
{
	struct image_description_info *image_descr_info = data;
	struct image_description *image_descr = image_descr_info->image_descr;

	image_descr_info_received(image_descr_info,
				  IMAGE_DESCR_INFO_EVENT_TARGET_MAXFALL);

	image_descr->target_max_fall = maxFALL;
}

static void
image_descr_info_icc_file_event(void *data,
				struct xx_image_description_info_v4 *xx_image_description_info_v4,
				int32_t icc_fd, uint32_t icc_size)
{
	struct image_description_info *image_descr_info = data;
	struct image_description *image_descr = image_descr_info->image_descr;

	image_descr_info_received(image_descr_info,
				  IMAGE_DESCR_INFO_EVENT_ICC_FD);

	image_descr->icc_fd = icc_fd;
	image_descr->icc_size = icc_size;
}

static bool
are_events_received_valid(struct image_description_info *image_descr_info)
{
	uint32_t events_received = image_descr_info->events_received;
	struct image_description *image_descr = image_descr_info->image_descr;

	/* ICC-based image description... */
	if ((events_received >> IMAGE_DESCR_INFO_EVENT_ICC_FD) & 1) {
		/* ...so we shouldn't have receive any other events. */
		if ((1 << IMAGE_DESCR_INFO_EVENT_ICC_FD) == events_received)
			return true;
		testlog("    Error: ICC image description but also received " \
			"parametric events\n");
		return false;
	}

	/* Non-ICC based image description, let's make sure that the received
	 * parameters make sense. */
	bool received_primaries, received_primaries_named;
	bool received_tf_power, received_tf_named;

	/* Should have received the primaries somewhow. */
	received_primaries_named = (events_received >>
				    IMAGE_DESCR_INFO_EVENT_PRIMARIES_NAMED) & 1;
	received_primaries = (events_received >>
			      IMAGE_DESCR_INFO_EVENT_PRIMARIES) & 1;
	if (!(received_primaries_named || received_primaries)) {
		testlog("    Error: parametric image description but no " \
			"primaries received\n");
		return false;
	}

	/* Should have received tf somehow. */
	received_tf_named = (events_received >>
			     IMAGE_DESCR_INFO_EVENT_TF_NAMED) & 1;
	received_tf_power = (events_received >>
			     IMAGE_DESCR_INFO_EVENT_TF_POWER_EXP) & 1;
	if (!(received_tf_named || received_tf_power)) {
		testlog("    Error: parametric image description but no " \
			" tf received\n");
		return false;
	}

	/* If we received tf named and exp power, they must match. */
	if (received_tf_named && received_tf_power) {
		if (image_descr->tf_named != XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_GAMMA22 &&
		    image_descr->tf_named != XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_GAMMA28) {
			testlog("    Error: parametric image description tf " \
				"named is not pure power-law, but still received " \
				"tf power event\n");
			return false;
		} else if (image_descr->tf_named == XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_GAMMA22 &&
			   image_descr->tf_power != 2.2f) {
			testlog("    Error: parametric image description tf named " \
				"is pure power-law 2.2, but tf power received is %f\n",
				image_descr->tf_power);
			return false;
		} else if (image_descr->tf_named == XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_GAMMA28 &&
			   image_descr->tf_power != 2.8f) {
			testlog("    Error: parametric image description tf named " \
				"is pure power-law 2.8, but tf power received is %f\n",
				image_descr->tf_power);
			return false;
		}
	}

	/* TODO: when target primaries, luminance, maxcll and maxfall are
	 * allowed? */

	return true;
}

static void
image_descr_info_done(void *data,
		      struct xx_image_description_info_v4 *xx_image_description_info_v4)
{
	struct image_description_info *image_descr_info = data;
	struct image_description *image_descr = image_descr_info->image_descr;

	testlog("Image description info %p done:\n", xx_image_description_info_v4);

	assert(are_events_received_valid(image_descr_info));

	/* ICC based image description */
	if ((image_descr_info->events_received >> IMAGE_DESCR_INFO_EVENT_ICC_FD) & 1) {
		testlog("    ICC file: fd %d, icc size %u.\n",
			image_descr->icc_fd, image_descr->icc_size);
		close(image_descr->icc_fd);
		return;
	}

	if ((image_descr_info->events_received >> IMAGE_DESCR_INFO_EVENT_PRIMARIES_NAMED) & 1)
		testlog("    Primaries named: %u\n", image_descr->primaries_named);

	if ((image_descr_info->events_received >> IMAGE_DESCR_INFO_EVENT_PRIMARIES) & 1)
		testlog("    Primary primaries:\n" \
			"        red         (x, y) = (%.4f, %.4f)\n" \
			"        green       (x, y) = (%.4f, %.4f)\n" \
			"        blue        (x, y) = (%.4f, %.4f)\n" \
			"        white point (x, y) = (%.4f, %.4f)\n",
			image_descr->primaries.primary[0].x,
			image_descr->primaries.primary[0].y,
			image_descr->primaries.primary[1].x,
			image_descr->primaries.primary[1].y,
			image_descr->primaries.primary[2].x,
			image_descr->primaries.primary[2].y,
			image_descr->primaries.white_point.x,
			image_descr->primaries.white_point.y);

	if ((image_descr_info->events_received >> IMAGE_DESCR_INFO_EVENT_TF_NAMED) & 1)
		testlog("    Transfer characteristics named: %u\n", image_descr->tf_named);

	if ((image_descr_info->events_received >> IMAGE_DESCR_INFO_EVENT_TF_POWER_EXP) & 1)
		testlog("    EOTF is a pure power-law curve of exp %.4f\n",
			   image_descr->tf_power);

	if ((image_descr_info->events_received >> IMAGE_DESCR_INFO_EVENT_TARGET_PRIMARIES) & 1)
		testlog("    Target primaries:\n" \
			"        red         (x, y) = (%.4f, %.4f)\n" \
			"        green       (x, y) = (%.4f, %.4f)\n" \
			"        blue        (x, y) = (%.4f, %.4f)\n" \
			"        white point (x, y) = (%.4f, %.4f)\n",
			image_descr->target_primaries.primary[0].x,
			image_descr->target_primaries.primary[0].y,
			image_descr->target_primaries.primary[1].x,
			image_descr->target_primaries.primary[1].y,
			image_descr->target_primaries.primary[2].x,
			image_descr->target_primaries.primary[2].y,
			image_descr->target_primaries.white_point.x,
			image_descr->target_primaries.white_point.y);

	if ((image_descr_info->events_received >> IMAGE_DESCR_INFO_EVENT_TARGET_LUMINANCE) & 1)
		testlog("    Target luminance: min: %.4f, max %.4f\n",
			image_descr->target_min_lum, image_descr->target_max_lum);

	if ((image_descr_info->events_received >> IMAGE_DESCR_INFO_EVENT_TARGET_MAXCLL) & 1)
		testlog("    Target maxCLL: %.4f\n", image_descr->target_max_cll);

	if ((image_descr_info->events_received >> IMAGE_DESCR_INFO_EVENT_TARGET_MAXFALL) & 1)
		testlog("    Target maxFALL: %.4f\n", image_descr->target_max_fall);
}

static const struct xx_image_description_info_v4_listener
image_descr_info_iface = {
	.primaries = image_descr_info_primaries,
	.primaries_named = image_descr_info_primaries_named,
	.tf_named = image_descr_info_tf_named,
	.tf_power = image_descr_info_tf_power,
	.target_primaries = image_descr_info_target_primaries,
	.target_luminance = image_descr_info_target_luminance,
	.target_max_cll = image_descr_info_target_max_cll,
	.target_max_fall = image_descr_info_target_max_fall,
	.icc_file = image_descr_info_icc_file_event,
	.done = image_descr_info_done,
};

static void
cm_supported_intent(void *data, struct xx_color_manager_v4 *xx_color_manager_v4,
		    uint32_t render_intent)
{
	struct color_manager *cm = data;

	cm->supported_rendering_intents |= (1 << render_intent);
}

static void
cm_supported_feature(void *data, struct xx_color_manager_v4 *xx_color_manager_v4,
		     uint32_t feature)
{
	struct color_manager *cm = data;

	cm->supported_features |= (1 << feature);
}

static void
cm_supported_tf_named(void *data, struct xx_color_manager_v4 *xx_color_manager_v4,
		      uint32_t tf_code)
{
	/* only used to create image descriptions using parameters, which is
	 * still unsupported by Weston. */
}

static void
cm_supported_primaries_named(void *data, struct xx_color_manager_v4 *xx_color_manager_v4,
			     uint32_t primaries_code)
{
	/* only used to create image descriptions using parameters, which is
	 * still unsupported by Weston. */
}

static const struct xx_color_manager_v4_listener
cm_iface = {
	.supported_intent = cm_supported_intent,
	.supported_feature = cm_supported_feature,
	.supported_tf_named = cm_supported_tf_named,
	.supported_primaries_named = cm_supported_primaries_named,
};

static void
color_manager_init(struct color_manager *cm, struct client *client)
{
	memset(cm, 0, sizeof(*cm));

	wl_list_init(&cm->image_descr_list);

        cm->manager = bind_to_singleton_global(client,
					       &xx_color_manager_v4_interface,
					       1);
	xx_color_manager_v4_add_listener(cm->manager, &cm_iface, cm);

	cm->output = xx_color_manager_v4_get_output(cm->manager,
						    client->output->wl_output);

	cm->surface = xx_color_manager_v4_get_surface(cm->manager,
						      client->surface->wl_surface);

	cm->feedback_surface =
		xx_color_manager_v4_get_feedback_surface(cm->manager,
							 client->surface->wl_surface);

	client_roundtrip(client);

	/* For now, Weston only supports the ICC image description creator. All
	 * the parametric parts of the protocol are still unsupported. */
	assert(cm->supported_features == (1 << XX_COLOR_MANAGER_V4_FEATURE_ICC_V2_V4));

	/* Weston supports all rendering intents. */
	assert(cm->supported_rendering_intents == ((1 << XX_COLOR_MANAGER_V4_RENDER_INTENT_PERCEPTUAL) |
						   (1 << XX_COLOR_MANAGER_V4_RENDER_INTENT_RELATIVE) |
						   (1 << XX_COLOR_MANAGER_V4_RENDER_INTENT_SATURATION) |
						   (1 << XX_COLOR_MANAGER_V4_RENDER_INTENT_ABSOLUTE) |
						   (1 << XX_COLOR_MANAGER_V4_RENDER_INTENT_RELATIVE_BPC)));
}

static void
color_manager_fini(struct color_manager *cm)
{
	struct image_description *image_descr, *tmp;

	wl_list_for_each_safe(image_descr, tmp, &cm->image_descr_list, link)
		image_description_destroy(image_descr);

	xx_color_management_output_v4_destroy(cm->output);
	xx_color_management_surface_v4_destroy(cm->surface);
	xx_color_management_feedback_surface_v4_destroy(cm->feedback_surface);
	xx_color_manager_v4_destroy(cm->manager);
}

static struct image_description *
get_output_image_description(struct color_manager *cm)
{
	struct image_description *image_descr = image_description_create();

	image_descr->xx_image_descr =
		xx_color_management_output_v4_get_image_description(cm->output);

	xx_image_description_v4_add_listener(image_descr->xx_image_descr,
					     &image_descr_iface, image_descr);

	wl_list_insert(&cm->image_descr_list, &image_descr->link);

	return image_descr;
}

static struct image_description *
get_surface_preferred_image_description(struct color_manager *cm)
{
	struct image_description *image_descr = image_description_create();

	image_descr->xx_image_descr =
		xx_color_management_feedback_surface_v4_get_preferred(cm->feedback_surface);

	xx_image_description_v4_add_listener(image_descr->xx_image_descr,
					     &image_descr_iface, image_descr);

	wl_list_insert(&cm->image_descr_list, &image_descr->link);

	return image_descr;
}

static struct image_description *
create_icc_based_image_description(struct color_manager *cm,
				   struct xx_image_description_creator_icc_v4 *image_descr_creator_icc,
				   const char *icc_path)
{
	struct image_description *image_descr = image_description_create();
	int32_t icc_fd;
	struct stat st;

	icc_fd = open(icc_path, O_RDONLY);
	assert(icc_fd >= 0);

	assert(fstat(icc_fd, &st) == 0);

	xx_image_description_creator_icc_v4_set_icc_file(image_descr_creator_icc,
							 icc_fd, 0, st.st_size);
	image_descr->xx_image_descr =
		xx_image_description_creator_icc_v4_create(image_descr_creator_icc);

	xx_image_description_v4_add_listener(image_descr->xx_image_descr,
					     &image_descr_iface, image_descr);

	wl_list_insert(&cm->image_descr_list, &image_descr->link);

	close(icc_fd);

	return image_descr;
}

static void
build_sRGB_icc_profile(const char *filename)
{
	cmsHPROFILE profile;
	double vcgt_exponents[COLOR_CHAN_NUM] = { 0.0 };
	bool saved;

	profile = build_lcms_matrix_shaper_profile_output(NULL, &pipeline_sRGB,
							  vcgt_exponents);
	assert(profile);

	saved = cmsSaveProfileToFile(profile, filename);
	assert(saved);

	cmsCloseProfile(profile);
}

static enum test_result_code
fixture_setup(struct weston_test_harness *harness)
{
	struct compositor_setup setup;

	compositor_setup_defaults(&setup);
        setup.renderer = WESTON_RENDERER_GL;
	setup.shell = SHELL_TEST_DESKTOP;
	setup.refresh = HIGHEST_OUTPUT_REFRESH;

	/* Create the sRGB ICC profile. We do that only once for this test
	 * program. */
	if (strlen(srgb_icc_profile_path) == 0) {
		char *tmp;

		tmp = output_filename_for_test_program(THIS_TEST_NAME,
						       NULL, "icm");
		assert(strlen(tmp) < ARRAY_LENGTH(srgb_icc_profile_path));
		strcpy(srgb_icc_profile_path, tmp);
		free(tmp);

		build_sRGB_icc_profile(srgb_icc_profile_path);
	}

	weston_ini_setup(&setup,
			 cfgln("[core]"),
			 cfgln("color-management=true"));

	return weston_test_harness_execute_as_client(harness, &setup);
}
DECLARE_FIXTURE_SETUP(fixture_setup);

TEST(smoke_test)
{
	struct client *client;
        struct color_manager cm;

	client = create_client_and_test_surface(100, 100, 100, 100);
	color_manager_init(&cm, client);

	color_manager_fini(&cm);
	client_destroy(client);
}

static void
image_descr_info_destroy(struct image_description_info *image_descr_info)
{
	xx_image_description_info_v4_destroy(image_descr_info->xx_image_description_info);
	free(image_descr_info);
}

static struct image_description_info *
image_descr_get_information(struct image_description *image_descr)
{
	struct image_description_info *image_descr_info;

	image_descr_info = xzalloc(sizeof(*image_descr_info));

	image_descr_info->image_descr = image_descr;

	image_descr_info->xx_image_description_info =
		xx_image_description_v4_get_information(image_descr->xx_image_descr);

	xx_image_description_info_v4_add_listener(image_descr_info->xx_image_description_info,
						  &image_descr_info_iface,
						  image_descr_info);

	return image_descr_info;
}

static void
wait_until_image_description_ready(struct client *client,
				   struct image_description *image_descr)
{
	while (image_descr->status == CM_IMAGE_DESC_NOT_CREATED)
		assert(wl_display_dispatch(client->wl_display) >= 0);

	assert(image_descr->status == CM_IMAGE_DESC_READY);
}

TEST(output_get_image_description)
{
	struct client *client;
	struct color_manager cm;
	struct image_description *image_descr;
	struct image_description_info *image_descr_info;

	client = create_client_and_test_surface(100, 100, 100, 100);
	color_manager_init(&cm, client);

	/* Get image description from output */
	image_descr = get_output_image_description(&cm);
	wait_until_image_description_ready(client, image_descr);

	/* Get output image description information */
	image_descr_info = image_descr_get_information(image_descr);
	client_roundtrip(client);

	image_descr_info_destroy(image_descr_info);
	color_manager_fini(&cm);
	client_destroy(client);
}

TEST(surface_get_preferred_image_description)
{
	struct client *client;
	struct color_manager cm;
	struct image_description *image_descr;
	struct image_description_info *image_descr_info;

	client = create_client_and_test_surface(100, 100, 100, 100);
	color_manager_init(&cm, client);

	/* Get preferred image description from surface */
	image_descr = get_surface_preferred_image_description(&cm);
	wait_until_image_description_ready(client, image_descr);

	/* Get surface image description information */
	image_descr_info = image_descr_get_information(image_descr);
	client_roundtrip(client);

	image_descr_info_destroy(image_descr_info);
	color_manager_fini(&cm);
	client_destroy(client);
}

TEST(create_parametric_image_description_creator_object)
{
	struct client *client;
	struct color_manager cm;
	struct xx_image_description_creator_params_v4 *param_creator;

	client = create_client_and_test_surface(100, 100, 100, 100);
	color_manager_init(&cm, client);

	/* Parametric image description creator is still unsupported */
	param_creator = xx_color_manager_v4_new_parametric_creator(cm.manager);
	expect_protocol_error(client, &xx_color_manager_v4_interface,
			      XX_COLOR_MANAGER_V4_ERROR_UNSUPPORTED_FEATURE);

	xx_image_description_creator_params_v4_destroy(param_creator);
	color_manager_fini(&cm);
	client_destroy(client);
}

TEST(create_image_description_before_setting_icc_file)
{
	struct client *client;
	struct color_manager cm;
	struct xx_image_description_creator_icc_v4 *image_descr_creator_icc;
	struct xx_image_description_v4 *image_desc;

	client = create_client_and_test_surface(100, 100, 100, 100);
	color_manager_init(&cm, client);

	image_descr_creator_icc =
		xx_color_manager_v4_new_icc_creator(cm.manager);

	/* Try creating image description based on ICC profile but without
	 * setting the ICC file, what should fail.
	 *
	 * We expect a protocol error from unknown object, because the
	 * image_descr_creator_icc wl_proxy will get destroyed with the create
	 * call below. It is a destructor request. */
	image_desc = xx_image_description_creator_icc_v4_create(image_descr_creator_icc);
	expect_protocol_error(client, NULL,
			      XX_IMAGE_DESCRIPTION_CREATOR_ICC_V4_ERROR_INCOMPLETE_SET);

	xx_image_description_v4_destroy(image_desc);
	color_manager_fini(&cm);
	client_destroy(client);
}

TEST(set_unreadable_icc_fd)
{
	struct client *client;
	struct color_manager cm;
	struct xx_image_description_creator_icc_v4 *image_descr_creator_icc;
	int32_t icc_fd;
	struct stat st;

	client = create_client_and_test_surface(100, 100, 100, 100);
	color_manager_init(&cm, client);

	image_descr_creator_icc =
		xx_color_manager_v4_new_icc_creator(cm.manager);

	/* The file is being open with WRITE, not READ permission. So the
	 * compositor should complain. */
	icc_fd = open(srgb_icc_profile_path, O_WRONLY);
	assert(icc_fd >= 0);
	assert(fstat(icc_fd, &st) == 0);

	/* Try setting the bad ICC file fd, it should fail. */
	xx_image_description_creator_icc_v4_set_icc_file(image_descr_creator_icc,
							 icc_fd, 0, st.st_size);
	expect_protocol_error(client, &xx_image_description_creator_icc_v4_interface,
			      XX_IMAGE_DESCRIPTION_CREATOR_ICC_V4_ERROR_BAD_FD);

	close(icc_fd);
	xx_image_description_creator_icc_v4_destroy(image_descr_creator_icc);
	color_manager_fini(&cm);
	client_destroy(client);
}

TEST(set_bad_icc_size_zero)
{
	struct client *client;
	struct color_manager cm;
	struct xx_image_description_creator_icc_v4 *image_descr_creator_icc;
	int32_t icc_fd;

	client = create_client_and_test_surface(100, 100, 100, 100);
	color_manager_init(&cm, client);

	image_descr_creator_icc =
		xx_color_manager_v4_new_icc_creator(cm.manager);

	icc_fd = open(srgb_icc_profile_path, O_RDONLY);
	assert(icc_fd >= 0);

	/* Try setting ICC file with a bad size, it should fail. */
	xx_image_description_creator_icc_v4_set_icc_file(image_descr_creator_icc,
							 icc_fd, 0, 0);
	expect_protocol_error(client, &xx_image_description_creator_icc_v4_interface,
			      XX_IMAGE_DESCRIPTION_CREATOR_ICC_V4_ERROR_BAD_SIZE);

	close(icc_fd);
	xx_image_description_creator_icc_v4_destroy(image_descr_creator_icc);
	color_manager_fini(&cm);
	client_destroy(client);
}

TEST(set_bad_icc_non_seekable)
{
	struct client *client;
	struct color_manager cm;
	struct xx_image_description_creator_icc_v4 *image_descr_creator_icc;
	int32_t fds[2];

	client = create_client_and_test_surface(100, 100, 100, 100);
	color_manager_init(&cm, client);

	image_descr_creator_icc =
		xx_color_manager_v4_new_icc_creator(cm.manager);

	/* We need a non-seekable file, and pipes are non-seekable. */
	assert(pipe(fds) >= 0);

	/* Pretend that it has a valid size of 1024 bytes. That still should
	 * fail because the fd is non-seekable. */
	xx_image_description_creator_icc_v4_set_icc_file(image_descr_creator_icc,
							 fds[0], 0, 1024);
	expect_protocol_error(client, &xx_image_description_creator_icc_v4_interface,
			      XX_IMAGE_DESCRIPTION_CREATOR_ICC_V4_ERROR_BAD_FD);

	close(fds[0]);
	close(fds[1]);
	xx_image_description_creator_icc_v4_destroy(image_descr_creator_icc);
	color_manager_fini(&cm);
	client_destroy(client);
}

TEST(set_icc_twice)
{
	struct client *client;
	struct color_manager cm;
	struct xx_image_description_creator_icc_v4 *image_descr_creator_icc;
	int32_t icc_fd;
	struct stat st;

	client = create_client_and_test_surface(100, 100, 100, 100);
	color_manager_init(&cm, client);

	image_descr_creator_icc =
		xx_color_manager_v4_new_icc_creator(cm.manager);

	icc_fd = open(srgb_icc_profile_path, O_RDONLY);
	assert(icc_fd >= 0);
	assert(fstat(icc_fd, &st) == 0);

	xx_image_description_creator_icc_v4_set_icc_file(image_descr_creator_icc,
							 icc_fd, 0, st.st_size);
	client_roundtrip(client);

	/* Set the ICC again, what should fail. */
	xx_image_description_creator_icc_v4_set_icc_file(image_descr_creator_icc,
							 icc_fd, 0, st.st_size);
	expect_protocol_error(client, &xx_image_description_creator_icc_v4_interface,
			      XX_IMAGE_DESCRIPTION_CREATOR_ICC_V4_ERROR_ALREADY_SET);

	close(icc_fd);
	xx_image_description_creator_icc_v4_destroy(image_descr_creator_icc);
	color_manager_fini(&cm);
	client_destroy(client);
}

TEST(create_icc_image_description_no_info)
{
	struct client *client;
	struct color_manager cm;
	struct xx_image_description_creator_icc_v4 *image_descr_creator_icc;
	struct image_description *image_descr;
	struct image_description_info *image_descr_info;

	client = create_client_and_test_surface(100, 100, 100, 100);
	color_manager_init(&cm, client);

	image_descr_creator_icc =
		xx_color_manager_v4_new_icc_creator(cm.manager);

	/* Create image description based on ICC profile */
	image_descr = create_icc_based_image_description(&cm, image_descr_creator_icc,
							 srgb_icc_profile_path);
	wait_until_image_description_ready(client, image_descr);

	/* Get image description information, and that should fail. Images
	 * descriptions that we create do not accept this request. */
	image_descr_info = image_descr_get_information(image_descr);
	expect_protocol_error(client, &xx_image_description_v4_interface,
			      XX_IMAGE_DESCRIPTION_V4_ERROR_NO_INFORMATION);

	image_descr_info_destroy(image_descr_info);
	color_manager_fini(&cm);
	client_destroy(client);
}

TEST(set_surface_image_description)
{
	struct client *client;
	struct color_manager cm;
	struct xx_image_description_creator_icc_v4 *image_descr_creator_icc;
	struct image_description *image_descr;

	client = create_client_and_test_surface(100, 100, 100, 100);
	color_manager_init(&cm, client);

	image_descr_creator_icc =
		xx_color_manager_v4_new_icc_creator(cm.manager);

	/* Create image description based on ICC profile */
	image_descr = create_icc_based_image_description(&cm, image_descr_creator_icc,
							 srgb_icc_profile_path);
	wait_until_image_description_ready(client, image_descr);

	/* Set surface image description */
	xx_color_management_surface_v4_set_image_description(cm.surface,
							     image_descr->xx_image_descr,
							     XX_COLOR_MANAGER_V4_RENDER_INTENT_PERCEPTUAL);
	client_roundtrip(client);

	color_manager_fini(&cm);
	client_destroy(client);
}
