/*
 * Copyright © 2012 Intel Corporation
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
#include <assert.h>
#include <float.h>
#include <math.h>
#include <string.h>

#include "shared/helpers.h"
#include "vertex-clipping.h"

struct clip_context {
	struct clipper_vertex prev;
	struct clipper_vertex box[2];
	struct clipper_vertex *vertices;
};

WESTON_EXPORT_FOR_TESTS float
clipper_float_difference(float a, float b)
{
	/* https://randomascii.wordpress.com/2012/02/25/comparing-floating-point-numbers-2012-edition/ */
	static const float max_diff = 4.0f * FLT_MIN;
	static const float max_rel_diff = 4.0e-5;
	float diff = a - b;
	float adiff = fabsf(diff);

	if (adiff <= max_diff)
		return 0.0f;

	a = fabsf(a);
	b = fabsf(b);
	if (adiff <= (a > b ? a : b) * max_rel_diff)
		return 0.0f;

	return diff;
}

/* A line segment (p1x, p1y)-(p2x, p2y) intersects the line x = x_arg.
 * Compute the y coordinate of the intersection.
 */
static float
clip_intersect_y(float p1x, float p1y, float p2x, float p2y,
		 float x_arg)
{
	float a;
	float diff = clipper_float_difference(p1x, p2x);

	/* Practically vertical line segment, yet the end points have already
	 * been determined to be on different sides of the line. Therefore
	 * the line segment is part of the line and intersects everywhere.
	 * Return the end point, so we use the whole line segment.
	 */
	if (diff == 0.0f)
		return p2y;

	a = (x_arg - p2x) / diff;
	return p2y + (p1y - p2y) * a;
}

/* A line segment (p1x, p1y)-(p2x, p2y) intersects the line y = y_arg.
 * Compute the x coordinate of the intersection.
 */
static float
clip_intersect_x(float p1x, float p1y, float p2x, float p2y,
		 float y_arg)
{
	float a;
	float diff = clipper_float_difference(p1y, p2y);

	/* Practically horizontal line segment, yet the end points have already
	 * been determined to be on different sides of the line. Therefore
	 * the line segment is part of the line and intersects everywhere.
	 * Return the end point, so we use the whole line segment.
	 */
	if (diff == 0.0f)
		return p2x;

	a = (y_arg - p2y) / diff;
	return p2x + (p1x - p2x) * a;
}

enum path_transition {
	PATH_TRANSITION_OUT_TO_OUT = 0,
	PATH_TRANSITION_OUT_TO_IN = 1,
	PATH_TRANSITION_IN_TO_OUT = 2,
	PATH_TRANSITION_IN_TO_IN = 3,
};

static void
clip_append_vertex(struct clip_context *ctx, float x, float y)
{
	ctx->vertices->x = x;
	ctx->vertices->y = y;
	ctx->vertices++;
}

static enum path_transition
path_transition_left_edge(struct clip_context *ctx, float x, float y)
{
	return ((ctx->prev.x >= ctx->box[0].x) << 1) | (x >= ctx->box[0].x);
}

static enum path_transition
path_transition_right_edge(struct clip_context *ctx, float x, float y)
{
	return ((ctx->prev.x < ctx->box[1].x) << 1) | (x < ctx->box[1].x);
}

static enum path_transition
path_transition_top_edge(struct clip_context *ctx, float x, float y)
{
	return ((ctx->prev.y >= ctx->box[0].y) << 1) | (y >= ctx->box[0].y);
}

static enum path_transition
path_transition_bottom_edge(struct clip_context *ctx, float x, float y)
{
	return ((ctx->prev.y < ctx->box[1].y) << 1) | (y < ctx->box[1].y);
}

static void
clip_polygon_leftright(struct clip_context *ctx,
		       enum path_transition transition,
		       float x, float y, float clip_x)
{
	float yi;

	switch (transition) {
	case PATH_TRANSITION_IN_TO_IN:
		clip_append_vertex(ctx, x, y);
		break;
	case PATH_TRANSITION_IN_TO_OUT:
		yi = clip_intersect_y(ctx->prev.x, ctx->prev.y, x, y, clip_x);
		clip_append_vertex(ctx, clip_x, yi);
		break;
	case PATH_TRANSITION_OUT_TO_IN:
		yi = clip_intersect_y(ctx->prev.x, ctx->prev.y, x, y, clip_x);
		clip_append_vertex(ctx, clip_x, yi);
		clip_append_vertex(ctx, x, y);
		break;
	case PATH_TRANSITION_OUT_TO_OUT:
		/* nothing */
		break;
	default:
		assert(0 && "bad enum path_transition");
	}

	ctx->prev.x = x;
	ctx->prev.y = y;
}

static void
clip_polygon_topbottom(struct clip_context *ctx,
		       enum path_transition transition,
		       float x, float y, float clip_y)
{
	float xi;

	switch (transition) {
	case PATH_TRANSITION_IN_TO_IN:
		clip_append_vertex(ctx, x, y);
		break;
	case PATH_TRANSITION_IN_TO_OUT:
		xi = clip_intersect_x(ctx->prev.x, ctx->prev.y, x, y, clip_y);
		clip_append_vertex(ctx, xi, clip_y);
		break;
	case PATH_TRANSITION_OUT_TO_IN:
		xi = clip_intersect_x(ctx->prev.x, ctx->prev.y, x, y, clip_y);
		clip_append_vertex(ctx, xi, clip_y);
		clip_append_vertex(ctx, x, y);
		break;
	case PATH_TRANSITION_OUT_TO_OUT:
		/* nothing */
		break;
	default:
		assert(0 && "bad enum path_transition");
	}

	ctx->prev.x = x;
	ctx->prev.y = y;
}

struct polygon8 {
	struct clipper_vertex pos[8];
	int n;
};

static void
clip_context_prepare(struct clip_context *ctx, const struct polygon8 *src,
		     struct clipper_vertex *dst)
{
	ctx->prev.x = src->pos[src->n - 1].x;
	ctx->prev.y = src->pos[src->n - 1].y;
	ctx->vertices = dst;
}

static int
clip_polygon_left(struct clip_context *ctx, const struct polygon8 *src,
		  struct clipper_vertex *dst)
{
	enum path_transition trans;
	int i;

	if (src->n < 2)
		return 0;

	clip_context_prepare(ctx, src, dst);
	for (i = 0; i < src->n; i++) {
		trans = path_transition_left_edge(ctx, src->pos[i].x, src->pos[i].y);
		clip_polygon_leftright(ctx, trans, src->pos[i].x, src->pos[i].y,
				       ctx->box[0].x);
	}
	return ctx->vertices - dst;
}

static int
clip_polygon_right(struct clip_context *ctx, const struct polygon8 *src,
		   struct clipper_vertex *dst)
{
	enum path_transition trans;
	int i;

	if (src->n < 2)
		return 0;

	clip_context_prepare(ctx, src, dst);
	for (i = 0; i < src->n; i++) {
		trans = path_transition_right_edge(ctx, src->pos[i].x, src->pos[i].y);
		clip_polygon_leftright(ctx, trans, src->pos[i].x, src->pos[i].y,
				       ctx->box[1].x);
	}
	return ctx->vertices - dst;
}

static int
clip_polygon_top(struct clip_context *ctx, const struct polygon8 *src,
		 struct clipper_vertex *dst)
{
	enum path_transition trans;
	int i;

	if (src->n < 2)
		return 0;

	clip_context_prepare(ctx, src, dst);
	for (i = 0; i < src->n; i++) {
		trans = path_transition_top_edge(ctx, src->pos[i].x, src->pos[i].y);
		clip_polygon_topbottom(ctx, trans, src->pos[i].x, src->pos[i].y,
				       ctx->box[0].y);
	}
	return ctx->vertices - dst;
}

static int
clip_polygon_bottom(struct clip_context *ctx, const struct polygon8 *src,
		    struct clipper_vertex *dst)
{
	enum path_transition trans;
	int i;

	if (src->n < 2)
		return 0;

	clip_context_prepare(ctx, src, dst);
	for (i = 0; i < src->n; i++) {
		trans = path_transition_bottom_edge(ctx, src->pos[i].x, src->pos[i].y);
		clip_polygon_topbottom(ctx, trans, src->pos[i].x, src->pos[i].y,
				       ctx->box[1].y);
	}
	return ctx->vertices - dst;
}

/* General purpose clipping function. Compute the boundary vertices of the
 * intersection of a 'polygon' and a clipping 'box'. 'polygon' points to an
 * array of 4 vertices defining a convex polygon of any winding order. 'box'
 * points to an array of 2 vertices where the values of the 1st vertex are less
 * than or equal to the values of the 2nd vertex. Up to 8 resulting vertices,
 * using 'polygon' winding order, are written to 'vertices'. The return value is
 * the number of vertices created.
 *
 * Based on Sutherland-Hodgman algorithm:
 * https://www.codeguru.com/cplusplus/polygon-clipping/
 */
static int
clip(const struct clipper_vertex polygon[4],
     const struct clipper_vertex box[2],
     struct clipper_vertex *restrict vertices)
{
	struct clip_context ctx;
	struct polygon8 p, tmp;
	int i, n;

	memcpy(ctx.box, box, 2 * sizeof *box);
	memcpy(p.pos, polygon, 4 * sizeof *polygon);
	p.n = 4;
	tmp.n = clip_polygon_left(&ctx, &p, tmp.pos);
	p.n = clip_polygon_right(&ctx, &tmp, p.pos);
	tmp.n = clip_polygon_top(&ctx, &p, tmp.pos);
	p.n = clip_polygon_bottom(&ctx, &tmp, p.pos);

	/* Get rid of duplicate vertices */
	vertices[0] = p.pos[0];
	n = 1;
	for (i = 1; i < p.n; i++) {
		if (clipper_float_difference(vertices[n - 1].x, p.pos[i].x) == 0.0f &&
		    clipper_float_difference(vertices[n - 1].y, p.pos[i].y) == 0.0f)
			continue;
		vertices[n] = p.pos[i];
		n++;
	}
	if (clipper_float_difference(vertices[n - 1].x, p.pos[0].x) == 0.0f &&
	    clipper_float_difference(vertices[n - 1].y, p.pos[0].y) == 0.0f)
		n--;

	return n;
}

WESTON_EXPORT_FOR_TESTS void
clipper_quad_init(struct clipper_quad *quad,
		  const struct clipper_vertex polygon[4],
		  bool axis_aligned)
{
	int i;

	memcpy(quad->polygon, polygon, 4 * sizeof *polygon);
	quad->axis_aligned = axis_aligned;

	if (axis_aligned)
		return;

	/* Find axis-aligned bounding box. */
	quad->bbox[0].x = quad->bbox[1].x = polygon[0].x;
	quad->bbox[0].y = quad->bbox[1].y = polygon[0].y;
	for (i = 1; i < 4; i++) {
		quad->bbox[0].x = MIN(quad->bbox[0].x, polygon[i].x);
		quad->bbox[1].x = MAX(quad->bbox[1].x, polygon[i].x);
		quad->bbox[0].y = MIN(quad->bbox[0].y, polygon[i].y);
		quad->bbox[1].y = MAX(quad->bbox[1].y, polygon[i].y);
	}
}

WESTON_EXPORT_FOR_TESTS int
clipper_quad_clip(struct clipper_quad *quad,
		  const struct clipper_vertex box[2],
		  struct clipper_vertex *restrict vertices)
{
	int i, n;

	/* Aligned case: quad edges are parallel to clipping box edges, there
	 * will be either four or zero edges. We just need to clamp the quad
	 * edges to the clipping box edges and test for non-zero area:
	 */
	if (quad->axis_aligned) {
		for (i = 0; i < 4; i++) {
			vertices[i].x = CLIP(quad->polygon[i].x,
					     box[0].x, box[1].x);
			vertices[i].y = CLIP(quad->polygon[i].y,
					     box[0].y, box[1].y);
		}
		if ((vertices[0].x != vertices[2].x) &&
		    (vertices[0].y != vertices[2].y))
			return 4;
		else
			return 0;
	}

	/* Unaligned case: first, simple bounding box check to discard early a
	 * quad that does not intersect with the clipping box:
	 */
	if ((quad->bbox[0].x >= box[1].x) || (quad->bbox[1].x <= box[0].x) ||
	    (quad->bbox[0].y >= box[1].y) || (quad->bbox[1].y <= box[0].y))
		return 0;

	/* Then use our general purpose clipping algorithm:
	 */
	n = clip(quad->polygon, box, vertices);

	if (n < 3)
		return 0;

	return n;
}

WESTON_EXPORT_FOR_TESTS int
clipper_quad_clip_box32(struct clipper_quad *quad,
			const struct pixman_box32 *box,
			struct clipper_vertex *restrict vertices)
{
	struct clipper_vertex box_vertices[2] = {
		{ box->x1, box->y1 },
		{ box->x2, box->y2 }
	};

	return clipper_quad_clip(quad, box_vertices, vertices);
}
