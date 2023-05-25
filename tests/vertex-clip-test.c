/*
 * Copyright © 2013 Sam Spilsbury <smspillaz@gmail.com>
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

#include "weston-test-runner.h"
#include "vertex-clipping.h"

#define BOUNDING_BOX_TOP_Y 100.0f
#define BOUNDING_BOX_LEFT_X 50.0f
#define BOUNDING_BOX_RIGHT_X 100.0f
#define BOUNDING_BOX_BOTTOM_Y 50.0f

#define INSIDE_X1 (BOUNDING_BOX_LEFT_X + 1.0f)
#define INSIDE_X2 (BOUNDING_BOX_RIGHT_X - 1.0f)
#define INSIDE_Y1 (BOUNDING_BOX_BOTTOM_Y + 1.0f)
#define INSIDE_Y2 (BOUNDING_BOX_TOP_Y - 1.0f)

#define OUTSIDE_X1 (BOUNDING_BOX_LEFT_X - 1.0f)
#define OUTSIDE_X2 (BOUNDING_BOX_RIGHT_X + 1.0f)
#define OUTSIDE_Y1 (BOUNDING_BOX_BOTTOM_Y - 1.0f)
#define OUTSIDE_Y2 (BOUNDING_BOX_TOP_Y + 1.0f)

static void
populate_clip_context (struct clip_context *ctx)
{
	ctx->box[0].x = BOUNDING_BOX_LEFT_X;
	ctx->box[0].y = BOUNDING_BOX_BOTTOM_Y;
	ctx->box[1].x = BOUNDING_BOX_RIGHT_X;
	ctx->box[1].y = BOUNDING_BOX_TOP_Y;
}

static int
clip_polygon (struct clip_context *ctx,
	      struct clip_vertex *polygon,
	      int n,
	      struct clip_vertex *vertices)
{
	populate_clip_context(ctx);
	return clip_transformed(ctx, polygon, n, vertices);
}

struct vertex_clip_test_data {
	struct clip_vertex polygon[8];
	struct clip_vertex clipped[8];
	int polygon_n;
	int clipped_n;
};

const struct vertex_clip_test_data test_data[] = {
	/* All inside */
	{
		.polygon = {
			{ INSIDE_X1, INSIDE_Y1 },
			{ INSIDE_X2, INSIDE_Y1 },
			{ INSIDE_X2, INSIDE_Y2 },
			{ INSIDE_X1, INSIDE_Y2 },
		},
		.clipped = {
			{ INSIDE_X1, INSIDE_Y1 },
			{ INSIDE_X2, INSIDE_Y1 },
			{ INSIDE_X2, INSIDE_Y2 },
			{ INSIDE_X1, INSIDE_Y2 },
		},
		.polygon_n = 4,
		.clipped_n = 4,
	},

	/* Top outside */
	{
		.polygon = {
			{ INSIDE_X1, INSIDE_Y1 },
			{ INSIDE_X2, INSIDE_Y1 },
			{ INSIDE_X2, OUTSIDE_Y2 },
			{ INSIDE_X1, OUTSIDE_Y2 },
		},
		.clipped = {
			{ INSIDE_X1, BOUNDING_BOX_TOP_Y },
			{ INSIDE_X1, INSIDE_Y1 },
			{ INSIDE_X2, INSIDE_Y1 },
			{ INSIDE_X2, BOUNDING_BOX_TOP_Y },
		},
		.polygon_n = 4,
		.clipped_n = 4,
	},

	/* Bottom outside */
	{
		.polygon = {
			{ INSIDE_X1, OUTSIDE_Y1 },
			{ INSIDE_X2, OUTSIDE_Y1 },
			{ INSIDE_X2, INSIDE_Y2 },
			{ INSIDE_X1, INSIDE_Y2 },
		},
		.clipped = {
			{ INSIDE_X1, BOUNDING_BOX_BOTTOM_Y },
			{ INSIDE_X2, BOUNDING_BOX_BOTTOM_Y },
			{ INSIDE_X2, INSIDE_Y2 },
			{ INSIDE_X1, INSIDE_Y2 },
		},
		.polygon_n = 4,
		.clipped_n = 4,
	},

	/* Left outside */
	{
		.polygon = {
			{ OUTSIDE_X1, INSIDE_Y1 },
			{ INSIDE_X2, INSIDE_Y1 },
			{ INSIDE_X2, INSIDE_Y2 },
			{ OUTSIDE_X1, INSIDE_Y2 },
		},
		.clipped = {
			{ BOUNDING_BOX_LEFT_X, INSIDE_Y1 },
			{ INSIDE_X2, INSIDE_Y1 },
			{ INSIDE_X2, INSIDE_Y2 },
			{ BOUNDING_BOX_LEFT_X, INSIDE_Y2 },
		},
		.polygon_n = 4,
		.clipped_n = 4,
	},

	/* Right outside */
	{
		.polygon = {
			{ INSIDE_X1, INSIDE_Y1 },
			{ OUTSIDE_X2, INSIDE_Y1 },
			{ OUTSIDE_X2, INSIDE_Y2 },
			{ INSIDE_X1, INSIDE_Y2 },
		},
		.clipped = {
			{ INSIDE_X1, INSIDE_Y1 },
			{ BOUNDING_BOX_RIGHT_X, INSIDE_Y1 },
			{ BOUNDING_BOX_RIGHT_X, INSIDE_Y2 },
			{ INSIDE_X1, INSIDE_Y2 },
		},
		.polygon_n = 4,
		.clipped_n = 4,
	},

	/* Diamond extending from bounding box edges */
	{
		.polygon = {
			{ BOUNDING_BOX_LEFT_X - 25, BOUNDING_BOX_BOTTOM_Y + 25 },
			{ BOUNDING_BOX_LEFT_X + 25, BOUNDING_BOX_TOP_Y + 25 },
			{ BOUNDING_BOX_RIGHT_X + 25, BOUNDING_BOX_TOP_Y - 25 },
			{ BOUNDING_BOX_RIGHT_X - 25, BOUNDING_BOX_BOTTOM_Y - 25 },
		},
		.clipped = {
			{ BOUNDING_BOX_LEFT_X, BOUNDING_BOX_BOTTOM_Y },
			{ BOUNDING_BOX_LEFT_X, BOUNDING_BOX_TOP_Y },
			{ BOUNDING_BOX_RIGHT_X, BOUNDING_BOX_TOP_Y },
			{ BOUNDING_BOX_RIGHT_X, BOUNDING_BOX_BOTTOM_Y },
		},
		.polygon_n = 4,
		.clipped_n = 4,
	},

	/* Diamond inside of bounding box edges */
	{
		.polygon = {
			{ BOUNDING_BOX_LEFT_X - 12.5, BOUNDING_BOX_BOTTOM_Y + 25 },
			{ BOUNDING_BOX_LEFT_X + 25, BOUNDING_BOX_TOP_Y + 12.5 },
			{ BOUNDING_BOX_RIGHT_X + 12.5, BOUNDING_BOX_TOP_Y - 25 },
			{ BOUNDING_BOX_RIGHT_X - 25, BOUNDING_BOX_BOTTOM_Y - 12.5 },
		},
		.clipped = {
			{ BOUNDING_BOX_LEFT_X + 12.5, BOUNDING_BOX_BOTTOM_Y },
			{ BOUNDING_BOX_LEFT_X, BOUNDING_BOX_BOTTOM_Y + 12.5 },
			{ BOUNDING_BOX_LEFT_X, BOUNDING_BOX_TOP_Y - 12.5 },
			{ BOUNDING_BOX_LEFT_X + 12.5, BOUNDING_BOX_TOP_Y },
			{ BOUNDING_BOX_RIGHT_X - 12.5, BOUNDING_BOX_TOP_Y },
			{ BOUNDING_BOX_RIGHT_X, BOUNDING_BOX_TOP_Y - 12.5 },
			{ BOUNDING_BOX_RIGHT_X, BOUNDING_BOX_BOTTOM_Y + 12.5 },
			{ BOUNDING_BOX_RIGHT_X - 12.5, BOUNDING_BOX_BOTTOM_Y },
		},
		.polygon_n = 4,
		.clipped_n = 8,
	},
};

TEST_P(clip_polygon_n_vertices_emitted, test_data)
{
	struct vertex_clip_test_data *tdata = data;
	struct clip_context ctx;
	struct clip_vertex clipped[8];
	int clipped_n;

	clipped_n = clip_polygon(&ctx, tdata->polygon, tdata->polygon_n,
				 clipped);

	assert(clipped_n == tdata->clipped_n);
}

TEST_P(clip_polygon_expected_vertices, test_data)
{
	struct vertex_clip_test_data *tdata = data;
	struct clip_context ctx;
	struct clip_vertex clipped[8];
	int clipped_n, i;

	clipped_n = clip_polygon(&ctx, tdata->polygon, tdata->polygon_n,
				 clipped);

	for (i = 0; i < clipped_n; i++) {
		assert(clipped[i].x == tdata->clipped[i].x);
		assert(clipped[i].y == tdata->clipped[i].y);
	}
}

TEST(clip_transformed_size_too_high)
{
	struct clip_context ctx;
	struct clip_vertex polygon[8] = {};

	assert(clip_transformed(&ctx, polygon, 9, NULL) == -1);
}

TEST(float_difference_different)
{
	assert(float_difference(1.0f, 0.0f) == 1.0f);
}

TEST(float_difference_same)
{
	assert(float_difference(1.0f, 1.0f) == 0.0f);
}

