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

#include "color.h"
#include "color-management.h"
#include "shared/string-helpers.h"
#include "shared/weston-assert.h"
#include "shared/xalloc.h"
#include "shared/helpers.h"

#include <fcntl.h>

#include "color-management-v1-server-protocol.h"

enum supports_get_info {
	NO_GET_INFO = false,
	YES_GET_INFO = true,
};

/**
 * This is the object that backs the image description abstraction from the
 * protocol. We may have multiple images descriptions for the same color
 * profile.
 *
 * Image description that we failed to create do not have such backing object.
 */
struct cm_image_desc {
	struct wl_resource *owner;
	struct weston_color_manager *cm;

	/* Reference to the color profile that it is backing up. An image
	 * description without a cprof is valid, and that simply means that it
	 * isn't ready (i.e. we didn't send the 'ready' event because we are
	 * still in the process of creating the color profile). */
	struct weston_color_profile *cprof;

	/* Depending how the image description is created, the protocol states
	 * that get_information() request should be invalid. */
	bool supports_get_info;
};

/**
 * Object created when get_info() is called for an image description object. It
 * gets destroyed when all the info is sent, i.e. with the done() event.
 */
struct cm_image_desc_info {
	struct wl_resource *owner;
	struct weston_compositor *compositor;
};

/**
 * When clients want to create image description based on ICC color profiles, we
 * use this struct to help.
 */
struct cm_creator_icc {
	struct wl_resource *owner;

	struct weston_compositor *compositor;

	/* ICC profile data given by the client. */
	int32_t icc_profile_fd;
	size_t icc_data_length;
	size_t icc_data_offset;
};

/**
 * For an ICC-based image description, sends the ICC information to the
 * client.
 *
 * If callers fail to create the fd for the ICC, they can call this function
 * with fd == -1 and it should return the proper error to clients.
 *
 * This is a helper function that should be used by the color plugin
 * that owns the color profile and has information about it.
 *
 * \param cm_image_desc_info The image description info object
 * \param fd The ICC profile file descriptor, or -1 in case of a failure
 * \param len The ICC profile size, in bytes
 */
WL_EXPORT void
weston_cm_send_icc_file(struct cm_image_desc_info *cm_image_desc_info,
			int32_t fd, uint32_t len)
{
	/* Caller failed to create fd. At this point we already know that the
	 * ICC is valid, so let's disconnect the client with OOM. */
	if (fd < 0) {
		wl_resource_post_no_memory(cm_image_desc_info->owner);
		return;
	}

	xx_image_description_info_v4_send_icc_file(cm_image_desc_info->owner,
						   fd, len);
}

/**
 * For a parametric image description, sends its
 * enum xx_color_manager_v4_primaries code to the client.
 *
 * This is a helper function that should be used by the color plugin
 * that owns the color profile and has information about it.
 *
 * \param cm_image_desc_info The image description info object
 * \param primaries_info The primaries_info object
 */
WL_EXPORT void
weston_cm_send_primaries_named(struct cm_image_desc_info *cm_image_desc_info,
			       const struct weston_color_primaries_info *primaries_info)
{
	xx_image_description_info_v4_send_primaries_named(cm_image_desc_info->owner,
							  primaries_info->protocol_primaries);
}

/**
 * For a parametric image description, sends the primary color volume primaries
 * and white point using CIE 1931 xy chromaticity coordinates to the client.
 *
 * This is a helper function that should be used by the color plugin
 * that owns the color profile and has information about it.
 *
 * \param cm_image_desc_info The image description info object
 * \param color_gamut The CIE 1931 xy chromaticity coordinates
 */
WL_EXPORT void
weston_cm_send_primaries(struct cm_image_desc_info *cm_image_desc_info,
			 const struct weston_color_gamut *color_gamut)
{
	xx_image_description_info_v4_send_primaries(cm_image_desc_info->owner,
						    /* red */
						    round(color_gamut->primary[0].x * 10000),
						    round(color_gamut->primary[0].y * 10000),
						    /* green */
						    round(color_gamut->primary[1].x * 10000),
						    round(color_gamut->primary[1].y * 10000),
						    /* blue */
						    round(color_gamut->primary[2].x * 10000),
						    round(color_gamut->primary[2].y * 10000),
						    /* white point */
						    round(color_gamut->white_point.x * 10000),
						    round(color_gamut->white_point.y * 10000));
}

/**
 * For a parametric image description, sends its
 * enum xx_color_manager_v4_transfer_function code to the client.
 *
 * This is a helper function that should be used by the color plugin
 * that owns the color profile and has information about it.
 *
 * \param cm_image_desc_info The image description info object
 * \param tf_info The tf_info object
 */
WL_EXPORT void
weston_cm_send_tf_named(struct cm_image_desc_info *cm_image_desc_info,
			const struct weston_color_tf_info *tf_info)
{
	xx_image_description_info_v4_send_tf_named(cm_image_desc_info->owner,
						   tf_info->protocol_tf);
}

/**
 * Destroy an image description info object.
 */
static void
cm_image_desc_info_destroy(struct cm_image_desc_info *cm_image_desc_info)
{
	free(cm_image_desc_info);
}

/**
 * Resource destruction function for the image description info. Destroys the
 * image description info backing object.
 */
static void
image_description_info_resource_destroy(struct wl_resource *cm_image_desc_info_res)
{
	struct cm_image_desc_info *cm_image_desc_info =
		wl_resource_get_user_data(cm_image_desc_info_res);

	cm_image_desc_info_destroy(cm_image_desc_info);
}

/**
 * Creates object to send information of a certain image description.
 */
static struct cm_image_desc_info *
image_description_info_create(struct wl_client *client, uint32_t version,
			      struct weston_compositor *compositor,
			      uint32_t cm_image_desc_info_id)
{
	struct cm_image_desc_info *cm_image_desc_info;

	cm_image_desc_info = xzalloc(sizeof(*cm_image_desc_info));

	cm_image_desc_info->compositor = compositor;

	cm_image_desc_info->owner =
		wl_resource_create(client, &xx_image_description_info_v4_interface,
				   version, cm_image_desc_info_id);
	if (!cm_image_desc_info->owner) {
		free(cm_image_desc_info);
		return NULL;
	}

	wl_resource_set_implementation(cm_image_desc_info->owner,
				       NULL, cm_image_desc_info,
				       image_description_info_resource_destroy);

	return cm_image_desc_info;
}

/**
 * Client wants the image description information.
 */
static void
image_description_get_information(struct wl_client *client,
				  struct wl_resource *cm_image_desc_res,
				  uint32_t cm_image_desc_info_id)
{
	struct cm_image_desc *cm_image_desc =
		wl_resource_get_user_data(cm_image_desc_res);
	uint32_t version = wl_resource_get_version(cm_image_desc_res);
	struct cm_image_desc_info *cm_image_desc_info;
	bool success;

	/* Invalid image description for this request, as we gracefully failed
	 * to create it. */
	if (!cm_image_desc) {
		wl_resource_post_error(cm_image_desc_res,
				       XX_IMAGE_DESCRIPTION_V4_ERROR_NOT_READY,
				       "we gracefully failed to create this image " \
				       "description");
		return;
	}

	/* Invalid image description for this request, as it isn't ready yet. */
	if (!cm_image_desc->cprof) {
		wl_resource_post_error(cm_image_desc_res,
				       XX_IMAGE_DESCRIPTION_V4_ERROR_NOT_READY,
				       "image description not ready yet");
		return;
	}

	/* Depending how the image description is created, the protocol states
	 * that get_information() request should be invalid. */
	if (!cm_image_desc->supports_get_info) {
		wl_resource_post_error(cm_image_desc_res,
				       XX_IMAGE_DESCRIPTION_V4_ERROR_NO_INFORMATION,
				       "get_information is not allowed for this "
				       "image description");
		return;
	}

	/* Create object responsible for sending the image description info. */
	cm_image_desc_info =
		image_description_info_create(client, version,
					      cm_image_desc->cm->compositor,
					      cm_image_desc_info_id);
	if (!cm_image_desc_info) {
		wl_resource_post_no_memory(cm_image_desc_res);
		return;
	}

	/* The color plugin is the one that has information about the color
	 * profile, so we go through it to send the info to clients. It uses
	 * our helpers (weston_cm_send_primaries(), etc) to do that. */
	success = cm_image_desc->cm->send_image_desc_info(cm_image_desc_info,
							  cm_image_desc->cprof);
	if (success)
		xx_image_description_info_v4_send_done(cm_image_desc_info->owner);

	/* All info sent, so destroy the object. */
	wl_resource_destroy(cm_image_desc_info->owner);
}

/**
 * Client will not use the image description anymore, so we destroy its
 * resource.
 */
static void
image_description_destroy(struct wl_client *client,
			  struct wl_resource *cm_image_desc_res)
{
	wl_resource_destroy(cm_image_desc_res);
}

static void
cm_image_desc_destroy(struct cm_image_desc *cm_image_desc);

/**
 * Resource destruction function for the image description. Destroys the image
 * description backing object.
 */
static void
image_description_resource_destroy(struct wl_resource *cm_image_desc_res)
{
	struct cm_image_desc *cm_image_desc =
		wl_resource_get_user_data(cm_image_desc_res);

	/* Image description that we failed to create do not have a backing
	 * struct cm_image_desc object. */
	if (!cm_image_desc)
		return;

	cm_image_desc_destroy(cm_image_desc);
}

static const struct xx_image_description_v4_interface
image_description_implementation = {
	.destroy = image_description_destroy,
	.get_information = image_description_get_information,
};

/**
 * Creates an image description object for a certain color profile.
 */
static struct cm_image_desc *
cm_image_desc_create(struct weston_color_manager *cm,
		     struct weston_color_profile *cprof,
		     struct wl_client *client, uint32_t version,
		     uint32_t image_description_id,
		     enum supports_get_info supports_get_info)
{
	struct cm_image_desc *cm_image_desc;

	cm_image_desc = xzalloc(sizeof(*cm_image_desc));

	cm_image_desc->owner =
		wl_resource_create(client, &xx_image_description_v4_interface,
				   version, image_description_id);
	if (!cm_image_desc->owner) {
		free(cm_image_desc);
		return NULL;
	}

	wl_resource_set_implementation(cm_image_desc->owner,
				       &image_description_implementation,
				       cm_image_desc,
				       image_description_resource_destroy);

	cm_image_desc->cm = cm;
	cm_image_desc->cprof = weston_color_profile_ref(cprof);
	cm_image_desc->supports_get_info = supports_get_info;

	return cm_image_desc;
}

/**
 * Destroy an image description object.
 */
static void
cm_image_desc_destroy(struct cm_image_desc *cm_image_desc)
{
	weston_color_profile_unref(cm_image_desc->cprof);
	free(cm_image_desc);
}

/**
 * Called by clients when they want to get the output's image description.
 */
static void
cm_output_get_image_description(struct wl_client *client,
				struct wl_resource *cm_output_res,
				uint32_t image_description_id)
{
	struct weston_head *head = wl_resource_get_user_data(cm_output_res);
	struct weston_compositor *compositor;
	struct weston_output *output;
	uint32_t version = wl_resource_get_version(cm_output_res);
	struct cm_image_desc *cm_image_desc;
	struct wl_resource *cm_image_desc_res;

	/* The protocol states that if the wl_output global (which is backed by
	 * the weston_head object) no longer exists, we should immediately send
	 * a "failed" event for the image desc. After receiving that, clients
	 * are not allowed to make requests other than "destroy" for the image
	 * description. So let's avoid creating a cm_image_desc object, let's
	 * create only the resource and send the failed event. */
	if (!head) {
		cm_image_desc_res =
			wl_resource_create(client, &xx_image_description_v4_interface,
					   version, image_description_id);
		if (!cm_image_desc_res) {
			wl_resource_post_no_memory(cm_output_res);
			return;
		}

		wl_resource_set_implementation(cm_image_desc_res,
					       &image_description_implementation,
					       NULL, image_description_resource_destroy);

		xx_image_description_v4_send_failed(cm_image_desc_res,
						    XX_IMAGE_DESCRIPTION_V4_CAUSE_NO_OUTPUT,
						    "the wl_output global no longer exists");
		return;
	}

	compositor = head->compositor;
	output = head->output;

	/* If the head becomes inactive (head->output == NULL), the respective
	 * wl_output global gets destroyed. In such case we make the cm_output
	 * object inert. We do that in weston_head_remove_global(), and the
	 * cm_output_res user data (which was the head itself) is set to NULL.
	 * So if we reached here, head is active and head->output != NULL. */
	weston_assert_ptr(compositor, output);

	cm_image_desc = cm_image_desc_create(compositor->color_manager,
					     output->color_profile, client,
					     version, image_description_id,
					     YES_GET_INFO);
	if (!cm_image_desc) {
		wl_resource_post_no_memory(cm_output_res);
		return;
	}

	xx_image_description_v4_send_ready(cm_image_desc->owner,
					   cm_image_desc->cprof->id);
}

/**
 * Client will not use the cm_output anymore, so we destroy its resource.
 */
static void
cm_output_destroy(struct wl_client *client, struct wl_resource *cm_output_res)
{
	wl_resource_destroy(cm_output_res);
}

/**
 * Resource destruction function for the cm_output.
 */
static void
cm_output_resource_destroy(struct wl_resource *cm_output_res)
{
	struct weston_head *head = wl_resource_get_user_data(cm_output_res);

	/* For inert cm_output, we don't have to do anything.
	 *
	 * If the cm_get_output() was called after we made the head inactive, we
	 * created the cm_output with no resource user data and didn't add the
	 * resource link to weston_head::cm_output_resource_list.
	 *
	 * If the cm_output was created with an active head but it became
	 * inactive later, we have already done what is necessary when cm_output
	 * became inert, in weston_head_remove_global(). */
	if (!head)
		return;

	/* We are destroying the cm_output_res, so simply remove it from
	 * weston_head::cm_output_resource_list. */
	wl_list_remove(wl_resource_get_link(cm_output_res));
}

static const struct xx_color_management_output_v4_interface
cm_output_implementation = {
	.destroy = cm_output_destroy,
	.get_image_description = cm_output_get_image_description,
};

/**
 * This function is called by libweston when the struct weston_output color
 * profile is updated.
 *
 * For each weston_head attached to the weston_output, we need to tell clients
 * that the cm_output image description has changed. Also, for each surface
 * whose primary output is the given, we need to send the preferred image
 * description changed event.
 *
 * If this is called during output initialization, this function is no-op. There
 * will be no client resources in weston_head::cm_output_resource_list and
 * neither surfaces whose primary output is the one we are dealing with.
 *
 * \param output The weston_output that changed the color profile.
 */
void
weston_output_send_image_description_changed(struct weston_output *output)
{
	struct weston_head *head;
	struct wl_resource *res;
	int ver;

	/* For each head attached to this weston_output, send the events that
	 * notifies that the output image description changed. */
	wl_list_for_each(head, &output->head_list, output_link) {
		wl_resource_for_each(res, &head->cm_output_resource_list)
			xx_color_management_output_v4_send_image_description_changed(res);

		/* wl_output.done should be sent after collecting all the
		 * changes related to the output. But in Weston we are lacking
		 * an atomic output configuration API, so we have no facilities
		 * to do that.
		 *
		 * TODO: enhance this behavior after we add the atomic output
		 * configuration API.
		 */
		wl_resource_for_each(res, &head->resource_list) {
			ver = wl_resource_get_version(res);
			if (ver >= WL_OUTPUT_DONE_SINCE_VERSION)
				wl_output_send_done(res);
		}
	}
}

/**
 * Client called get_output(). We already have the backing object, so just
 * create a resource for the client.
 */
static void
cm_get_output(struct wl_client *client, struct wl_resource *cm_res,
	      uint32_t cm_output_id, struct wl_resource *output_res)
{
	struct weston_head *head = weston_head_from_resource(output_res);
	uint32_t version = wl_resource_get_version(cm_res);
	struct wl_resource *res;

	res = wl_resource_create(client, &xx_color_management_output_v4_interface,
				 version, cm_output_id);
	if (!res) {
		wl_resource_post_no_memory(cm_res);
		return;
	}

	/* Client wants the cm_output but we've already made the head inactive,
	 * so let's set the implementation data as NULL. */
	if (!head) {
		wl_resource_set_implementation(res, &cm_output_implementation,
					       NULL, cm_output_resource_destroy);
		return;
	}

	wl_resource_set_implementation(res, &cm_output_implementation,
				       head, cm_output_resource_destroy);

	wl_list_insert(&head->cm_output_resource_list,
		       wl_resource_get_link(res));
}

/**
 * Called by clients to update the image description of a surface.
 *
 * If the surface state is commited, libweston will update the struct
 * weston_surface color profile and render intent.
 */
static void
cm_surface_set_image_description(struct wl_client *client,
				 struct wl_resource *cm_surface_res,
				 struct wl_resource *cm_image_desc_res,
				 uint32_t protocol_render_intent)
{
	struct weston_surface *surface = wl_resource_get_user_data(cm_surface_res);
	struct cm_image_desc *cm_image_desc =
		wl_resource_get_user_data(cm_image_desc_res);
	struct weston_color_manager *cm;
	const struct weston_render_intent_info *render_intent;

	/* The surface might have been already gone, in such case cm_surface is
	 * inert. */
	if (!surface) {
		/* TODO: This error will be surface intert in the future */
		wl_resource_post_error(cm_surface_res,
				       XX_COLOR_MANAGEMENT_SURFACE_V4_ERROR_IMAGE_DESCRIPTION,
				       "the wl_surface has already been destroyed");
		return;
	}

	/* Invalid image description for this request, as we gracefully failed
	 * to create it. */
	if (!cm_image_desc) {
		/* TODO: the version of the xx protocol that we are using still
		 * does not have an error for this. Fix when we update to the
		 * next version. */
		wl_resource_post_no_memory(cm_surface_res);
		return;
	}

	/* Invalid image description for this request, as it isn't ready yet. */
	if (!cm_image_desc->cprof) {
		wl_resource_post_error(cm_surface_res,
				       XX_COLOR_MANAGEMENT_SURFACE_V4_ERROR_IMAGE_DESCRIPTION,
				       "the image description is not ready");
		return;
	}

	cm = cm_image_desc->cm;

	render_intent = weston_render_intent_info_from_protocol(surface->compositor,
								protocol_render_intent);
	if (!render_intent) {
		wl_resource_post_error(cm_surface_res,
				       XX_COLOR_MANAGEMENT_SURFACE_V4_ERROR_RENDER_INTENT,
				       "unknown render intent");
		return;
	}

	if (!((cm->supported_rendering_intents >> render_intent->intent) & 1)) {
		wl_resource_post_error(cm_surface_res,
				       XX_COLOR_MANAGEMENT_SURFACE_V4_ERROR_RENDER_INTENT,
				       "unsupported render intent");
		return;
	}

	weston_color_profile_unref(surface->pending.color_profile);
	surface->pending.color_profile =
		weston_color_profile_ref(cm_image_desc->cprof);
	surface->pending.render_intent = render_intent;
}

/**
 * Called by clients to unset the image description.
 *
 * If the surface state is commited, libweston will update the struct
 * weston_surface color profile and render intent.
 */
static void
cm_surface_unset_image_description(struct wl_client *client,
				   struct wl_resource *cm_surface_res)
{
	struct weston_surface *surface = wl_resource_get_user_data(cm_surface_res);

	/* The surface might have been already gone, in such case cm_surface is
	 * inert. */
	if (!surface) {
		/* TODO: This error will be surface intert in the future */
		wl_resource_post_error(cm_surface_res,
				       XX_COLOR_MANAGEMENT_SURFACE_V4_ERROR_IMAGE_DESCRIPTION,
				       "the wl_surface has already been destroyed");
		return;
	}

	weston_color_profile_unref(surface->pending.color_profile);
	surface->pending.color_profile = NULL;
	surface->pending.render_intent = NULL;
}

/**
 * Client will not use the cm_surface anymore, so we destroy its resource.
 */
static void
cm_surface_destroy(struct wl_client *client, struct wl_resource *cm_surface_res)
{
	wl_resource_destroy(cm_surface_res);
}

/**
 * Resource destruction function for the cm_surface.
 */
static void
cm_surface_resource_destroy(struct wl_resource *cm_surface_res)
{
	struct weston_surface *surface = wl_resource_get_user_data(cm_surface_res);

	/* For inert cm_surface, we don't have to do anything.
	 *
	 * We already did what was necessary when cm_surface became inert, in
	 * the surface destruction process (in weston_surface_unref(), which
	 * is the surface destruction function). */
	if (!surface)
		return;

	surface->cm_surface = NULL;

	/* Do the same as unset_image_description */
	weston_color_profile_unref(surface->pending.color_profile);
	surface->pending.color_profile = NULL;
	surface->pending.render_intent = NULL;
}

static const struct xx_color_management_surface_v4_interface
cm_surface_implementation = {
	.destroy = cm_surface_destroy,
	.set_image_description = cm_surface_set_image_description,
	.unset_image_description = cm_surface_unset_image_description,
};

/**
 * Client called get_surface(). We already have the backing object, so just
 * create a resource for the client.
 */
static void
cm_get_surface(struct wl_client *client, struct wl_resource *cm_res,
	       uint32_t cm_surface_id, struct wl_resource *surface_res)
{
	struct weston_surface *surface = wl_resource_get_user_data(surface_res);
	uint32_t version = wl_resource_get_version(cm_res);
	struct wl_resource *res;

	if (surface->cm_surface) {
		wl_resource_post_error(cm_res,
				       XX_COLOR_MANAGER_V4_ERROR_SURFACE_EXISTS,
				       "surface already requested");
		return;
	}

	res = wl_resource_create(client, &xx_color_management_surface_v4_interface,
				 version, cm_surface_id);
	if (!res) {
		wl_resource_post_no_memory(cm_res);
		return;
	}

	wl_resource_set_implementation(res, &cm_surface_implementation,
				       surface, cm_surface_resource_destroy);

	surface->cm_surface = res;
}

/**
 * Client will not use the cm_feedback_surface anymore, so we destroy its resource.
 */
static void
cm_feedback_surface_destroy(struct wl_client *client,
			    struct wl_resource *cm_feedback_surface_res)
{
	wl_resource_destroy(cm_feedback_surface_res);
}

/**
 * Called by clients when they want to know the preferred image description of
 * the surface.
 */
static void
cm_feedback_surface_get_preferred(struct wl_client *client,
				  struct wl_resource *cm_feedback_surface_res,
				  uint32_t image_description_id)
{
	struct weston_surface *surface = wl_resource_get_user_data(cm_feedback_surface_res);
	uint32_t version = wl_resource_get_version(cm_feedback_surface_res);
	struct weston_color_manager *cm;
	struct cm_image_desc *cm_image_desc;

	/* The surface might have been already gone, in such case cm_feedback_surface is
	 * inert. */
	if (!surface) {
		wl_resource_post_error(cm_feedback_surface_res,
				       XX_COLOR_MANAGEMENT_FEEDBACK_SURFACE_V4_ERROR_INERT,
				       "the wl_surface has already been destroyed");
		return;
	}

	cm = surface->compositor->color_manager;

	cm_image_desc = cm_image_desc_create(cm, surface->preferred_color_profile,
					     client, version, image_description_id,
					     YES_GET_INFO);
	if (!cm_image_desc) {
		wl_resource_post_no_memory(cm_feedback_surface_res);
		return;
	}

	xx_image_description_v4_send_ready(cm_image_desc->owner,
					   cm_image_desc->cprof->id);
}

static const struct xx_color_management_feedback_surface_v4_interface
cm_feedback_surface_implementation = {
	.destroy = cm_feedback_surface_destroy,
	.get_preferred = cm_feedback_surface_get_preferred,
};

/**
 * Resource destruction function for the cm_feedback_surface.
 */
static void
cm_feedback_surface_resource_destroy(struct wl_resource *cm_feedback_surface_res)
{
	struct weston_surface *surface = wl_resource_get_user_data(cm_feedback_surface_res);

	/* For inert cm_feedback_surface, we don't have to do anything.
	 *
	 * We already did what was necessary when cm_feedback_surface became
	 * inert, in  the surface destruction process (in weston_surface_unref(),
	 * which is the surface destruction function). */
	if (!surface)
		return;

	/* We are destroying the cm_feedback_surface_res, so simply remove it from
	 * weston_surface::cm_feedback_surface_resource_list. */
	wl_list_remove(wl_resource_get_link(cm_feedback_surface_res));
}

/**
 * Notifies clients that their surface preferred image description changed.
 *
 * \param surface The surface that changed its preferred image description.
 */
void
weston_surface_send_preferred_image_description_changed(struct weston_surface *surface)
{
	struct wl_resource *res;

	/* For each resource, send the event that notifies that the surface
	 * preferred image description changed. */
	wl_resource_for_each(res, &surface->cm_feedback_surface_resource_list)
		xx_color_management_feedback_surface_v4_send_preferred_changed(res);
}

/**
 * Client called get_feedback_surface(). We already have the backing object, so just
 * create a resource for the client.
 */
static void
cm_get_feedback_surface(struct wl_client *client, struct wl_resource *cm_res,
			uint32_t cm_surface_id, struct wl_resource *surface_res)
{
	struct weston_surface *surface = wl_resource_get_user_data(surface_res);
	uint32_t version = wl_resource_get_version(cm_res);
	struct wl_resource *res;

	res = wl_resource_create(client, &xx_color_management_feedback_surface_v4_interface,
				 version, cm_surface_id);
	if (!res) {
		wl_resource_post_no_memory(cm_res);
		return;
	}

	wl_resource_set_implementation(res, &cm_feedback_surface_implementation,
				       surface, cm_feedback_surface_resource_destroy);
	wl_list_insert(&surface->cm_feedback_surface_resource_list, wl_resource_get_link(res));
}

/**
 * Sets the ICC file for the ICC-based image description creator object.
 */
static void
cm_creator_icc_set_icc_file(struct wl_client *client,
			    struct wl_resource *resource,
			    int32_t icc_profile_fd,
			    uint32_t offset, uint32_t length)
{
	struct cm_creator_icc *cm_creator_icc = wl_resource_get_user_data(resource);
	int flags;
	uint32_t err_code;
	const char *err_msg;

	if (cm_creator_icc->icc_data_length > 0) {
		err_code = XX_IMAGE_DESCRIPTION_CREATOR_ICC_V4_ERROR_ALREADY_SET;
		err_msg = "ICC file was already set";
		goto err;
	}

	/* Length should be in the (0, 4MB] interval */
	if (length == 0 || length > (4 * 1024 * 1024)) {
		err_code = XX_IMAGE_DESCRIPTION_CREATOR_ICC_V4_ERROR_BAD_SIZE;
		err_msg = "invalid ICC file size";
		goto err;
	}

	/* Fd should be readable. */
	flags = fcntl(icc_profile_fd, F_GETFL);
	if ((flags & O_ACCMODE) == O_WRONLY) {
		err_code = XX_IMAGE_DESCRIPTION_CREATOR_ICC_V4_ERROR_BAD_FD;
		err_msg = "ICC fd is not readable";
		goto err;
	}

	/* Fd should be seekable. */
	if (lseek(icc_profile_fd, 0, SEEK_CUR) < 0) {
		err_code = XX_IMAGE_DESCRIPTION_CREATOR_ICC_V4_ERROR_BAD_FD;
		err_msg = "ICC fd is not seekable";
		goto err;
	}

	cm_creator_icc->icc_profile_fd = icc_profile_fd;

	/* We save length and offset in size_t variables. This ensures that they
	 * fit. We received them as uint32_t from the protocol. */
	static_assert(UINT32_MAX <= SIZE_MAX,
		      "won't be able to save uint32_t var into size_t");
	cm_creator_icc->icc_data_length = length;
	cm_creator_icc->icc_data_offset = offset;

	return;

err:
	close(icc_profile_fd);
	wl_resource_post_error(resource, err_code, "%s", err_msg);
}

static bool
do_length_and_offset_fit(struct cm_creator_icc *cm_creator_icc)
{
	size_t end;
	off_t end_off;

	/* Ensure that length + offset doesn't overflow in size_t. If that isn't
	 * true, we won't be able to make it fit into off_t. And we may need
	 * that to read the ICC file. */
	if (cm_creator_icc->icc_data_length > SIZE_MAX - cm_creator_icc->icc_data_offset)
		return false;

	/* Ensure that length + offset doesn't overflow in off_t. */
	end = cm_creator_icc->icc_data_offset + cm_creator_icc->icc_data_length;
	end_off = end;
	if (end_off < 0 || end != (size_t)end_off)
		return false;

	return true;
}

static int
create_image_description_color_profile_from_icc_creator(struct cm_image_desc *cm_image_desc,
							struct cm_creator_icc *cm_creator_icc)
{
	struct weston_compositor *compositor = cm_creator_icc->compositor;
	struct weston_color_manager *cm = compositor->color_manager;
	struct weston_color_profile *cprof;
	char *err_msg;
	void *icc_prof_data;
	size_t bytes_read = 0;
	ssize_t pread_ret;
	bool ret;

	if (!do_length_and_offset_fit(cm_creator_icc)) {
		xx_image_description_v4_send_failed(cm_image_desc->owner,
						    XX_IMAGE_DESCRIPTION_V4_CAUSE_OPERATING_SYSTEM,
						    "length + offset does not fit off_t");
		return -1;
	}

	/* Create buffer to read ICC profile. As they may have up to 4Mb, we
	 * send OOM if something fails (instead of using xalloc). */
	icc_prof_data = zalloc(cm_creator_icc->icc_data_length);
	if (!icc_prof_data) {
		wl_resource_post_no_memory(cm_creator_icc->owner);
		return -1;
	}

	/* Read ICC file.
	 *
	 * TODO: it is not that simple. Clients can abuse that to DoS the
	 * compositor. See the discussion in the link below.
	 *
	 * https://gitlab.freedesktop.org/wayland/weston/-/merge_requests/1356#note_2125102
	 */
	while (bytes_read < cm_creator_icc->icc_data_length) {
		pread_ret = pread(cm_creator_icc->icc_profile_fd,
				  icc_prof_data + bytes_read,
				  cm_creator_icc->icc_data_length - bytes_read,
				  (off_t)cm_creator_icc->icc_data_offset + bytes_read);
		if (pread_ret < 0) {
			/* Failed to read but not an error (just interruption),
			 * so continue trying to read. */
			if (errno == EINTR)
				continue;

			/* Reading the ICC failed */
			free(icc_prof_data);
			str_printf(&err_msg, "failed to read ICC file: %s", strerror(errno));
			xx_image_description_v4_send_failed(cm_image_desc->owner,
							    XX_IMAGE_DESCRIPTION_V4_CAUSE_OPERATING_SYSTEM,
							    err_msg);
			free(err_msg);
			return -1;
		} else if (pread_ret == 0) {
			/* We were expecting to read more than 0 bytes, but we
			 * didn't. That means that we've tried to read beyond
			 * EOF. This is client's fault, it must make sure that
			 * the given ICC file don't simply change. */
			free(icc_prof_data);
			wl_resource_post_error(cm_creator_icc->owner,
					       XX_IMAGE_DESCRIPTION_CREATOR_ICC_V4_ERROR_OUT_OF_FILE,
					       "tried to read ICC beyond EOF");
			return -1;
		}
		bytes_read += (size_t)pread_ret;
	}
	weston_assert_true(compositor, bytes_read == cm_creator_icc->icc_data_length);

	/* We've read the ICC file so let's create the color profile. */
	ret = cm->get_color_profile_from_icc(cm, icc_prof_data,
					     cm_creator_icc->icc_data_length,
					     "icc-from-client", &cprof, &err_msg);
	free(icc_prof_data);

	if (!ret) {
		/* We can't tell if it is client's fault that the ICC profile is
		 * invalid, so let's gracefully fail without returning a
		 * protocol error.
		 *
		 * TODO: we need to return proper error codes from the
		 * color-manager plugins and decide if we should gracefully fail
		 * or return a protocol error.
		 */
		xx_image_description_v4_send_failed(cm_image_desc->owner,
						    XX_IMAGE_DESCRIPTION_V4_CAUSE_UNSUPPORTED,
						    err_msg);
		free(err_msg);
		return -1;
	}

	cm_image_desc->cprof = cprof;
	xx_image_description_v4_send_ready(cm_image_desc->owner,
					   cm_image_desc->cprof->id);
	return 0;
}

/**
 * Creates image description using the ICC-based image description creator
 * object. This is a destructor type request, so the cm_creator_icc resource
 * gets destroyed after this.
 */
static void
cm_creator_icc_create(struct wl_client *client, struct wl_resource *resource,
		      uint32_t image_description_id)
{
	struct cm_creator_icc *cm_creator_icc =
		wl_resource_get_user_data(resource);
	struct weston_compositor *compositor = cm_creator_icc->compositor;
	struct weston_color_manager *cm = compositor->color_manager;
	uint32_t version = wl_resource_get_version(cm_creator_icc->owner);
	struct cm_image_desc *cm_image_desc;
	int ret;

	if (cm_creator_icc->icc_data_length == 0) {
		wl_resource_post_error(resource,
				       XX_IMAGE_DESCRIPTION_CREATOR_ICC_V4_ERROR_INCOMPLETE_SET,
				       "trying to create image description before " \
				       "setting the ICC file");
		return;
	}

	/* Create the image description with cprof == NULL. */
	cm_image_desc = cm_image_desc_create(cm, NULL, client, version,
					     image_description_id, NO_GET_INFO);
	if (!cm_image_desc) {
		wl_resource_post_no_memory(resource);
		return;
	}

	/* Create the cprof for the image description. */
	ret = create_image_description_color_profile_from_icc_creator(cm_image_desc,
								      cm_creator_icc);
	if (ret < 0) {
		/* If something went wrong and we failed to create the image
		 * description, let's set the resource userdata to NULL. We use
		 * that to be able to tell if a client is trying to use an
		 * (invalid) image description that we failed to create. */
		wl_resource_set_user_data(cm_image_desc->owner, NULL);
		cm_image_desc_destroy(cm_image_desc);
	}

	/* Destroy the cm_creator_icc resource. This is a destructor request. */
	wl_resource_destroy(cm_creator_icc->owner);
}

/**
 * Resource destruction function for the cm_creator_icc.
 * It should only destroy itself, but not the image description it creates.
 */
static void
cm_creator_icc_destructor(struct wl_resource *resource)
{
	struct cm_creator_icc *cm_creator_icc =
		wl_resource_get_user_data(resource);

	if (cm_creator_icc->icc_profile_fd >= 0)
		close(cm_creator_icc->icc_profile_fd);

	free(cm_creator_icc);
}

static const struct xx_image_description_creator_icc_v4_interface
cm_creator_icc_implementation = {
	.create = cm_creator_icc_create,
	.set_icc_file = cm_creator_icc_set_icc_file,
};

/**
 * Creates an ICC-based image description creator for the client.
 */
static void
cm_new_image_description_creator_icc(struct wl_client *client, struct wl_resource *cm_res,
				     uint32_t cm_creator_icc_id)
{
	struct cm_creator_icc *cm_creator_icc;
	struct weston_compositor *compositor = wl_resource_get_user_data(cm_res);
	struct weston_color_manager *cm = compositor->color_manager;
	uint32_t version = wl_resource_get_version(cm_res);

	if (!((cm->supported_color_features >> WESTON_COLOR_FEATURE_ICC) & 1)) {
		wl_resource_post_error(cm_res, XX_COLOR_MANAGER_V4_ERROR_UNSUPPORTED_FEATURE,
				       "creating ICC image description creator is " \
				       "still unsupported");
		return;
	}

	cm_creator_icc = xzalloc(sizeof(*cm_creator_icc));

	cm_creator_icc->compositor = compositor;
	cm_creator_icc->icc_profile_fd = -1;

	cm_creator_icc->owner =
		wl_resource_create(client, &xx_image_description_creator_icc_v4_interface,
				   version, cm_creator_icc_id);
	if (!cm_creator_icc->owner)
		goto err;

	wl_resource_set_implementation(cm_creator_icc->owner, &cm_creator_icc_implementation,
				       cm_creator_icc, cm_creator_icc_destructor);

	return;

err:
	free(cm_creator_icc);
	wl_resource_post_no_memory(cm_res);
}

/**
 * Creates a parametric image description creator for the client.
 */
static void
cm_new_image_description_creator_params(struct wl_client *client, struct wl_resource *cm_res,
					uint32_t cm_creator_params_id)
{
	/* Still unsupported. */
	wl_resource_post_error(cm_res, XX_COLOR_MANAGER_V4_ERROR_UNSUPPORTED_FEATURE,
			       "creating parametric image description creator is " \
			       "still unsupported");
}

/**
 * Client will not use the color management object anymore, so we destroy its
 * resource. That should not affect the other objects in any way.
 */
static void
cm_destroy(struct wl_client *client, struct wl_resource *cm_res)
{
	wl_resource_destroy(cm_res);
}

static const struct xx_color_manager_v4_interface
color_manager_implementation = {
	.destroy = cm_destroy,
	.get_output = cm_get_output,
	.get_surface = cm_get_surface,
	.get_feedback_surface = cm_get_feedback_surface,
	.new_icc_creator = cm_new_image_description_creator_icc,
	.new_parametric_creator = cm_new_image_description_creator_params,
};

/**
 * Called when clients bind to the color-management protocol.
 */
static void
bind_color_management(struct wl_client *client, void *data, uint32_t version,
		      uint32_t id)
{
	struct wl_resource *resource;
	struct weston_compositor *compositor = data;
	struct weston_color_manager *cm = compositor->color_manager;
	const struct weston_color_feature_info *feature_info;
	const struct weston_render_intent_info *render_intent;
	unsigned int i;

	resource = wl_resource_create(client, &xx_color_manager_v4_interface,
				      version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &color_manager_implementation,
				       compositor, NULL);

	/* Expose the supported color features to the client. */
	for (i = 0; i < 32; i++) {
		if (!((cm->supported_color_features >> i) & 1))
			continue;
		feature_info = weston_color_feature_info_from(compositor, i);
		xx_color_manager_v4_send_supported_feature(resource,
							   feature_info->protocol_feature);
	}

	/* Expose the supported rendering intents to the client. */
	for (i = 0; i < 32; i++) {
		if (!((cm->supported_rendering_intents >> i) & 1))
			continue;
		render_intent = weston_render_intent_info_from(compositor, i);
		xx_color_manager_v4_send_supported_intent(resource,
							  render_intent->protocol_intent);
	}
}

/** Advertise color-management support
 *
 * Calling this initializes the color-management protocol support, so that
 * xx_color_manager_v4_interface will be advertised to clients. Essentially it
 * creates a global. Do not call this function multiple times in the
 * compositor's lifetime. There is no way to deinit explicitly, globals will be
 * reaped when the wl_display gets destroyed.
 *
 * \param compositor The compositor to init for.
 * \return Zero on success, -1 on failure.
 */
int
weston_compositor_enable_color_management_protocol(struct weston_compositor *compositor)
{
	uint32_t version = 1;

	weston_assert_bit_is_set(compositor,
				 compositor->color_manager->supported_rendering_intents,
				 WESTON_RENDER_INTENT_PERCEPTUAL);

	if (!wl_global_create(compositor->wl_display,
			      &xx_color_manager_v4_interface,
			      version, compositor, bind_color_management))
		return -1;

	return 0;
}
