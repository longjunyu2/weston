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

struct vertex_clip_test_data {
	struct clipper_vertex box[2];
	struct clipper_vertex polygon[8];
	struct clipper_vertex clipped[8];
	int polygon_n;
	int clipped_n;
};

const struct vertex_clip_test_data test_data[] = {
	/* All inside */
	{
		.box = {
			{ BOUNDING_BOX_LEFT_X, BOUNDING_BOX_BOTTOM_Y },
			{ BOUNDING_BOX_RIGHT_X, BOUNDING_BOX_TOP_Y },
		},
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
		.box = {
			{ BOUNDING_BOX_LEFT_X, BOUNDING_BOX_BOTTOM_Y },
			{ BOUNDING_BOX_RIGHT_X, BOUNDING_BOX_TOP_Y },
		},
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
		.box = {
			{ BOUNDING_BOX_LEFT_X, BOUNDING_BOX_BOTTOM_Y },
			{ BOUNDING_BOX_RIGHT_X, BOUNDING_BOX_TOP_Y },
		},
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
		.box = {
			{ BOUNDING_BOX_LEFT_X, BOUNDING_BOX_BOTTOM_Y },
			{ BOUNDING_BOX_RIGHT_X, BOUNDING_BOX_TOP_Y }
		},
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
		.box = {
			{ BOUNDING_BOX_LEFT_X, BOUNDING_BOX_BOTTOM_Y },
			{ BOUNDING_BOX_RIGHT_X, BOUNDING_BOX_TOP_Y },
		},
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
		.box = {
			{ BOUNDING_BOX_LEFT_X, BOUNDING_BOX_BOTTOM_Y },
			{ BOUNDING_BOX_RIGHT_X, BOUNDING_BOX_TOP_Y },
		},
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
		.box = {
			{ BOUNDING_BOX_LEFT_X, BOUNDING_BOX_BOTTOM_Y },
			{ BOUNDING_BOX_RIGHT_X, BOUNDING_BOX_TOP_Y },
		},
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
	struct clipper_vertex clipped[8];
	int clipped_n;

	clipped_n = clipper_clip(tdata->polygon, tdata->polygon_n, tdata->box,
				 clipped);

	assert(clipped_n == tdata->clipped_n);
}

TEST_P(clip_polygon_expected_vertices, test_data)
{
	struct vertex_clip_test_data *tdata = data;
	struct clipper_vertex clipped[8];
	int clipped_n, i;

	clipped_n = clipper_clip(tdata->polygon, tdata->polygon_n, tdata->box,
				 clipped);

	for (i = 0; i < clipped_n; i++) {
		assert(clipped[i].x == tdata->clipped[i].x);
		assert(clipped[i].y == tdata->clipped[i].y);
	}
}

TEST(clip_size_too_high)
{
	struct clipper_vertex polygon[8] = {}, box[2] = {};

	assert(clipper_clip(polygon, 9, box, NULL) == -1);
}

TEST(float_difference_different)
{
	assert(clipper_float_difference(1.0f, 0.0f) == 1.0f);
}

TEST(float_difference_same)
{
	assert(clipper_float_difference(1.0f, 1.0f) == 0.0f);
}

