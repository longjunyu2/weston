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

/*
 * Each vertex clipping test begins with a brief textual introduction using a
 * coordinate system with X growing right and Y growing down as a convention.
 */

#include "config.h"

#include "weston-test-runner.h"
#include "vertex-clipping.h"

#define BOX(x1,y1,x2,y2)    { { x1, y1 }, { x2, y2 } }
#define QUAD(x1,y1,x2,y2)   { { x1, y1 }, { x2, y1 }, { x2, y2 }, { x1, y2 } }

struct vertex_clip_test_data {
	struct clipper_vertex box[2];
	struct clipper_vertex polygon[8];
	struct clipper_vertex clipped[8];
	int polygon_n;
	int clipped_n;
};

/* Compare clipped vertices to expected vertices. While the clipper guarantees
 * correct winding order, it doesn't specify which vertex is emitted first. This
 * function takes care of finding the first expected vertex in the clipped array
 * before comparing the entire series. */
static void
assert_vertices(const struct clipper_vertex *clipped, int clipped_n,
		const struct clipper_vertex *expected, int expected_n)
{
	int first, i, j;

	/* Is the number of clipped vertices correct? */
	assert(clipped_n == expected_n);

	for (first = 0; first < clipped_n; first++)
		if (clipper_float_difference(clipped[first].x, expected[0].x) == 0.0f &&
		    clipper_float_difference(clipped[first].y, expected[0].y) == 0.0f)
			break;

	/* Have we found the first expected vertex? */
	assert(!clipped_n || first != clipped_n);

	/* Do the remaining vertices match? */
	for (i = 1; i < clipped_n; i++) {
		j = (i + first) % clipped_n;
		assert(clipper_float_difference(clipped[j].x, expected[i].x) == 0.0f &&
		       clipper_float_difference(clipped[j].y, expected[i].y) == 0.0f);
	}
}

/* clipper_clip() tests: */

static const struct vertex_clip_test_data clip_expected_data[] = {
	/* Quad inside box. */
	{
		.box       = BOX (50.0f, 50.0f, 100.0f, 100.0f),
		.polygon   = QUAD(51.0f, 51.0f,  99.0f,  99.0f),
		.clipped   = QUAD(51.0f, 51.0f,  99.0f,  99.0f),
		.polygon_n = 4,
		.clipped_n = 4,
	},

	/* Quad bottom edge outside of box. */
	{
		.box       = BOX (50.0f, 50.0f, 100.0f, 100.0f),
		.polygon   = QUAD(51.0f, 51.0f,  99.0f, 101.0f),
		.clipped   = QUAD(51.0f, 51.0f,  99.0f, 100.0f),
		.polygon_n = 4,
		.clipped_n = 4,
	},

	/* Quad top edge outside of box. */
	{
		.box       = BOX (50.0f, 50.0f, 100.0f, 100.0f),
		.polygon   = QUAD(51.0f, 49.0f,  99.0f,  99.0f),
		.clipped   = QUAD(51.0f, 50.0f,  99.0f,  99.0f),
		.polygon_n = 4,
		.clipped_n = 4,
	},

	/* Quad left edge outside of box. */
	{
		.box       = BOX (50.0f, 50.0f, 100.0f, 100.0f),
		.polygon   = QUAD(49.0f, 51.0f,  99.0f,  99.0f),
		.clipped   = QUAD(50.0f, 51.0f,  99.0f,  99.0f),
		.polygon_n = 4,
		.clipped_n = 4,
	},

	/* Quad right edge outside of box. */
	{
		.box       = BOX (50.0f, 50.0f, 100.0f, 100.0f),
		.polygon   = QUAD(51.0f, 51.0f, 101.0f,  99.0f),
		.clipped   = QUAD(51.0f, 51.0f, 100.0f,  99.0f),
		.polygon_n = 4,
		.clipped_n = 4,
	},

	/* Rotated quad with edges adjacent to box corners. */
	{
		.box       = BOX(50.0f, 50.0f, 100.0f, 100.0f),
		.polygon   = {{ 25.0f, 75.0f}, {75.0f,  25.0f},
			      {125.0f, 75.0f}, {75.0f, 125.0f}},
		.clipped   = QUAD(50.0f, 50.0f, 100.0f, 100.0f),
		.polygon_n = 4,
		.clipped_n = 4,
	},

	/* Rotated quad with edges cutting out box corners. */
	{
		.box       = BOX(50.0f, 50.0f, 100.0f, 100.0f),
		.polygon   = {{ 37.5f,  75.0f}, { 75.0f,  37.5f},
			      {112.5f,  75.0f}, { 75.0f, 112.5f}},
		.clipped   = {{ 62.5f,  50.0f}, { 87.5f,  50.0f},
			      {100.0f,  62.5f}, {100.0f,  87.5f},
			      { 87.5f, 100.0f}, { 62.5f, 100.0f},
			      { 50.0f,  87.5f}, { 50.0f,  62.5f}},
		.polygon_n = 4,
		.clipped_n = 8,
	},

	/* Same as above using counter-clockwise winding. */
	{
		.box       = BOX(50.0f, 50.0f, 100.0f, 100.0f),
		.polygon   = {{ 37.5f,  75.0f}, { 75.0f, 112.5f},
			      {112.5f,  75.0f}, { 75.0f,  37.5f}},
		.clipped   = {{ 62.5f,  50.0f}, { 50.0f,  62.5f},
			      { 50.0f,  87.5f}, { 62.5f, 100.0f},
			      { 87.5f, 100.0f}, {100.0f,  87.5f},
			      {100.0f,  62.5f}, { 87.5f,  50.0f}},
		.polygon_n = 4,
		.clipped_n = 8,
	},
};

TEST_P(clip_expected, clip_expected_data)
{
	struct vertex_clip_test_data *tdata = data;
	struct clipper_vertex clipped[8];
	int clipped_n;

	clipped_n = clipper_clip(tdata->polygon, tdata->polygon_n, tdata->box,
				 clipped);

	assert_vertices(clipped, clipped_n, tdata->clipped, tdata->clipped_n);
}

TEST(clip_size_too_high)
{
	struct clipper_vertex polygon[8] = {}, box[2] = {};

	assert(clipper_clip(polygon, 9, box, NULL) == -1);
}

/* clipper_float_difference() tests: */

TEST(float_difference_different)
{
	assert(clipper_float_difference(1.0f, 0.0f) == 1.0f);
}

TEST(float_difference_same)
{
	assert(clipper_float_difference(1.0f, 1.0f) == 0.0f);
}
