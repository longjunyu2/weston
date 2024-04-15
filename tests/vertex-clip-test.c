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
#define BOX32(x1,y1,x2,y2)  { x1, y1, x2, y2 }
#define QUAD(x1,y1,x2,y2)   { { x1, y1 }, { x2, y1 }, { x2, y2 }, { x1, y2 } }

struct vertex_clip_test_data {
	union {
		struct clipper_vertex box[2]; /* Common clipping API. */
		struct pixman_box32 box32;    /* Pixman clipping API. */
	};
	struct clipper_vertex polygon[8];
	struct clipper_vertex clipped[8];
	int clipped_n;
	bool aligned;
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

/* clipper_quad_clip() tests: */

static const struct vertex_clip_test_data quad_clip_expected_data[] = {
	/* Aligned quad clipping with adjacent edges: */

	/* Box top/left corner adjacent to polygon bottom/right corner. */
	{
		.aligned   = true,
		.box       = BOX (-0.50f, -0.50f,  0.50f,  0.50f),
		.polygon   = QUAD(-1.00f, -1.00f, -0.50f, -0.50f),
		.clipped_n = 0,
	},

	/* Box top edge adjacent to polygon bottom edge. */
	{
		.aligned   = true,
		.box       = BOX (-0.50f, -0.50f,  0.50f,  0.50f),
		.polygon   = QUAD(-0.25f, -1.00f,  0.25f, -0.50f),
		.clipped_n = 0,
	},

	/* Box top/right corner adjacent to polygon bottom/left corner. */
	{
		.aligned   = true,
		.box       = BOX (-0.50f, -0.50f,  0.50f,  0.50f),
		.polygon   = QUAD( 0.50f, -1.00f,  1.00f, -0.50f),
		.clipped_n = 0,
	},

	/* Box left edge adjacent to polygon right edge. */
	{
		.aligned   = true,
		.box       = BOX (-0.50f, -0.50f,  0.50f,  0.50f),
		.polygon   = QUAD(-1.00f, -0.25f, -0.50f,  0.25f),
		.clipped_n = 0,
	},

	/* Box right edge adjacent to polygon left edge. */
	{
		.aligned   = true,
		.box       = BOX (-0.50f, -0.50f,  0.50f,  0.50f),
		.polygon   = QUAD( 0.50f, -0.25f,  1.00f,  0.25f),
		.clipped_n = 0,
	},

	/* Box bottom/left corner adjacent to polygon top/right corner. */
	{
		.aligned   = true,
		.box       = BOX (-0.50f, -0.50f,  0.50f,  0.50f),
		.polygon   = QUAD(-1.00f,  0.50f, -0.50f,  1.00f),
		.clipped_n = 0,
	},

	/* Box bottom edge adjacent to polygon top edge. */
	{
		.aligned   = true,
		.box       = BOX (-0.50f, -0.50f,  0.50f,  0.50f),
		.polygon   = QUAD(-0.25f,  0.50f,  0.25f,  1.00f),
		.clipped_n = 0,
	},

	/* Box bottom/right corner adjacent to polygon top/left corner. */
	{
		.aligned   = true,
		.box       = BOX (-0.50f, -0.50f,  0.50f,  0.50f),
		.polygon   = QUAD( 0.50f,  0.50f,  1.00f,  1.00f),
		.clipped_n = 0,
	},

	/* Aligned quad clipping with intersecting edges: */

	/* Box top/left corner intersects polygon bottom/right corner. */
	{
		.aligned   = true,
		.box       = BOX (-0.50f, -0.50f,  0.50f,  0.50f),
		.polygon   = QUAD(-0.75f, -0.75f, -0.25f, -0.25f),
		.clipped   = QUAD(-0.50f, -0.50f, -0.25f, -0.25f),
		.clipped_n = 4,
	},

	/* Box top edge intersects polygon bottom edge. */
	{
		.aligned   = true,
		.box       = BOX (-0.50f, -0.50f,  0.50f,  0.50f),
		.polygon   = QUAD(-0.25f, -0.75f,  0.25f, -0.25f),
		.clipped   = QUAD(-0.25f, -0.50f,  0.25f, -0.25f),
		.clipped_n = 4,
	},

	/* Box top/right corner intersects polygon bottom/left corner. */
	{
		.aligned   = true,
		.box       = BOX (-0.50f, -0.50f,  0.50f,  0.50f),
		.polygon   = QUAD( 0.25f, -0.75f,  0.75f, -0.25f),
		.clipped   = QUAD( 0.25f, -0.50f,  0.50f, -0.25f),
		.clipped_n = 4,
	},

	/* Box left edge intersects polygon right edge. */
	{
		.aligned   = true,
		.box       = BOX (-0.50f, -0.50f,  0.50f,  0.50f),
		.polygon   = QUAD(-0.75f, -0.25f, -0.25f,  0.25f),
		.clipped   = QUAD(-0.50f, -0.25f, -0.25f,  0.25f),
		.clipped_n = 4,
	},

	/* Box right edge intersects polygon left edge. */
	{
		.aligned   = true,
		.box       = BOX (-0.50f, -0.50f,  0.50f,  0.50f),
		.polygon   = QUAD( 0.25f, -0.25f,  0.75f,  0.25f),
		.clipped   = QUAD( 0.25f, -0.25f,  0.50f,  0.25f),
		.clipped_n = 4,
	},

	/* Box bottom/left corner intersects polygon top/right corner. */
	{
		.aligned   = true,
		.box       = BOX (-0.50f, -0.50f,  0.50f,  0.50f),
		.polygon   = QUAD(-0.75f,  0.25f, -0.25f,  0.75f),
		.clipped   = QUAD(-0.50f,  0.25f, -0.25f,  0.50f),
		.clipped_n = 4,
	},

	/* Box bottom edge intersects polygon top edge. */
	{
		.aligned   = true,
		.box       = BOX (-0.50f, -0.50f,  0.50f,  0.50f),
		.polygon   = QUAD(-0.25f,  0.25f,  0.25f,  0.75f),
		.clipped   = QUAD(-0.25f,  0.25f,  0.25f,  0.50f),
		.clipped_n = 4,
	},

	/* Box bottom/right corner intersects polygon top/left corner. */
	{
		.aligned   = true,
		.box       = BOX (-0.50f, -0.50f,  0.50f,  0.50f),
		.polygon   = QUAD( 0.25f,  0.25f,  0.75f,  0.75f),
		.clipped   = QUAD( 0.25f,  0.25f,  0.50f,  0.50f),
		.clipped_n = 4,
	},

	/* Rotated quad clipping with adjacent edges: */

	/* Box top/left corner adjacent to polygon bottom corner. */
	{
		.aligned   = false,
		.box       = BOX(-0.5f, -0.5f, 0.5f, 0.5f),
		.polygon   = {{-0.75f, -0.75f}, {-0.50f, -1.00f},
			      {-0.25f, -0.75f}, {-0.50f, -0.50f}},
		.clipped_n = 0,
	},

	/* Box top edge adjacent to polygon bottom corner. */
	{
		.aligned   = false,
		.box       = BOX(-0.5f, -0.5f, 0.5f, 0.5f),
		.polygon   = {{-0.25f, -0.75f}, { 0.00f, -1.00f},
			      { 0.25f, -0.75f}, { 0.00f, -0.50f}},
		.clipped_n = 0,
	},

	/* Box top/right corner adjacent to polygon bottom corner. */
	{
		.aligned   = false,
		.box       = BOX(-0.5f, -0.5f, 0.5f, 0.5f),
		.polygon   = {{ 0.25f, -0.75f}, { 0.50f, -1.00f},
			      { 0.75f, -0.75f}, { 0.50f, -0.50f}},
		.clipped_n = 0,
	},

	/* Box left edge adjacent to polygon right corner. */
	{
		.aligned   = false,
		.box       = BOX(-0.5f, -0.5f, 0.5f, 0.5f),
		.polygon   = {{-1.00f,  0.00f}, {-0.75f, -0.25f},
			      {-0.50f,  0.00f}, {-0.75f,  0.25f}},
		.clipped_n = 0,
	},

	/* Box right edge adjacent to polygon left corner. */
	{
		.aligned   = false,
		.box       = BOX(-0.5f, -0.5f, 0.5f, 0.5f),
		.polygon   = {{ 0.50f,  0.00f}, { 0.75f, -0.25f},
			      { 1.00f,  0.00f}, { 0.75f,  0.25f}},
		.clipped_n = 0,
	},

	/* Box bottom/left corner adjacent to polygon top corner. */
	{
		.aligned   = false,
		.box       = BOX(-0.5f, -0.5f, 0.5f, 0.5f),
		.polygon   = {{-0.75f,  0.75f}, {-0.50f,  0.50f},
			      {-0.25f,  0.75f}, {-0.50f,  1.00f}},
		.clipped_n = 0,
	},

	/* Box bottom edge adjacent to polygon top corner. */
	{
		.aligned   = false,
		.box       = BOX(-0.5f, -0.5f, 0.5f, 0.5f),
		.polygon   = {{-0.25f,  0.75f}, { 0.00f,  0.50f},
			      { 0.25f,  0.75f}, { 0.00f,  1.00f}},
		.clipped_n = 0,
	},

	/* Box bottom/right corner adjacent to polygon top corner. */
	{
		.aligned   = false,
		.box       = BOX(-0.5f, -0.5f, 0.5f, 0.5f),
		.polygon   = {{ 0.25f,  0.75f}, { 0.50f,  0.50f},
			      { 0.75f,  0.75f}, { 0.50f,  1.00f}},
		.clipped_n = 0,
	},

	/* Rotated quad clipping with slightly intersecting edges: */

	/* Box top/left corner slightly intersects polygon bottom/right edge. */
	{
		.aligned   = false,
		.box       = BOX(-0.5f, -0.5f, 0.5f, 0.5f),
		.polygon   = {{-0.75f, -0.50f}, {-0.50f, -0.75f},
			      {-0.25f, -0.50f}, {-0.50f, -0.25f}},
		.clipped   = {{-0.50f, -0.25f}, {-0.50f, -0.50f},
			      {-0.25f, -0.50f}},
		.clipped_n = 3,
	},

	/* Box top edge slightly intersects polygon bottom corner. */
	{
		.aligned   = false,
		.box       = BOX(-0.5f, -0.5f, 0.5f, 0.5f),
		.polygon   = {{-0.25f, -0.50f}, { 0.00f, -0.75f},
			      { 0.25f, -0.50f}, { 0.00f, -0.25f}},
		.clipped   = {{-0.25f, -0.50f}, { 0.25f, -0.50f},
			      { 0.00f, -0.25f}},
		.clipped_n = 3,
	},

	/* Box top/right corner slightly intersects polygon bottom/left edge. */
	{
		.aligned   = false,
		.box       = BOX(-0.5f, -0.5f, 0.5f, 0.5f),
		.polygon   = {{ 0.25f, -0.50f}, { 0.50f, -0.75f},
			      { 0.75f, -0.50f}, { 0.50f, -0.25f}},
		.clipped   = {{ 0.25f, -0.50f}, { 0.50f, -0.50f},
			      { 0.50f, -0.25f}},
		.clipped_n = 3,
	},

	/* Box left edge slightly intersects polygon right corner. */
	{
		.aligned   = false,
		.box       = BOX(-0.5f, -0.5f, 0.5f, 0.5f),
		.polygon   = {{-0.75f,  0.00f}, {-0.50f, -0.25f},
			      {-0.25f,  0.00f}, {-0.50f,  0.25f}},
		.clipped   = {{-0.50f, -0.25f}, {-0.25f,  0.00f},
			      {-0.50f,  0.25f}},
		.clipped_n = 3,
	},

	/* Box right edge slightly intersects polygon left corner. */
	{
		.aligned   = false,
		.box       = BOX(-0.5f, -0.5f, 0.5f, 0.5f),
		.polygon   = {{ 0.25f,  0.00f}, { 0.50f, -0.25f},
			      { 0.75f,  0.00f}, { 0.50f,  0.25f}},
		.clipped   = {{ 0.25f,  0.00f}, { 0.50f, -0.25f},
			      { 0.50f,  0.25f}},
		.clipped_n = 3,
	},

	/* Box bottom/left corner slightly intersects polygon top/right edge. */
	{
		.aligned   = false,
		.box       = BOX(-0.5f, -0.5f, 0.5f, 0.5f),
		.polygon   = {{-0.75f,  0.50f}, {-0.50f,  0.25f},
			      {-0.25f,  0.50f}, {-0.50f,  0.75f}},
		.clipped   = {{-0.50f,  0.25f}, {-0.25f,  0.50f},
			      {-0.50f,  0.50f}},
		.clipped_n = 3,
	},

	/* Box bottom edge slightly intersects polygon top corner. */
	{
		.aligned   = false,
		.box       = BOX(-0.5f, -0.5f, 0.5f, 0.5f),
		.polygon   = {{-0.25f,  0.50f}, { 0.00f,  0.25f},
			      { 0.25f,  0.50f}, { 0.00f,  0.75f}},
		.clipped   = {{-0.25f,  0.50f}, { 0.00f,  0.25f},
			      { 0.25f,  0.50f}},
		.clipped_n = 3,
	},

	/* Box bottom/right corner slightly intersects polygon top/left edge. */
	{
		.aligned   = false,
		.box       = BOX(-0.5f, -0.5f, 0.5f, 0.5f),
		.polygon   = {{ 0.25f,  0.50f}, { 0.50f,  0.25f},
			      { 0.75f,  0.50f}, { 0.50f,  0.75f}},
		.clipped   = {{ 0.25f,  0.50f}, { 0.50f,  0.25f},
			      { 0.50f,  0.50f}},
		.clipped_n = 3,
	},

	/* Rotated quad clipping with largely intersecting edges: */

	/* Box top/left corner largely intersects polygon bottom/right edge. */
	{
		.aligned   = false,
		.box       = BOX(-0.5f, -0.5f, 0.5f, 0.5f),
		.polygon   = {{-0.625f, -0.375f}, {-0.375f, -0.625f},
			      {-0.125f, -0.375f}, {-0.375f, -0.125f}},
		.clipped   = {{-0.500f, -0.500f}, {-0.250f, -0.500f},
			      {-0.125f, -0.375f}, {-0.375f, -0.125f},
			      {-0.500f, -0.250f}},
		.clipped_n = 5,
	},

	/* Box top edge largely intersects polygon bottom corner. */
	{
		.aligned   = false,
		.box       = BOX(-0.5f, -0.5f, 0.5f, 0.5f),
		.polygon   = {{-0.250f, -0.375f}, { 0.000f, -0.625f},
			      { 0.250f, -0.375f}, { 0.000f, -0.125f}},
		.clipped   = {{-0.125f, -0.500f}, { 0.125f, -0.500f},
			      { 0.250f, -0.375f}, { 0.000f, -0.125f},
			      {-0.250f, -0.375f}},
		.clipped_n = 5,
	},

	/* Box top/right corner largely intersects polygon bottom/left edge. */
	{
		.aligned   = false,
		.box       = BOX(-0.5f, -0.5f, 0.5f, 0.5f),
		.polygon   = {{ 0.125f, -0.375f}, { 0.375f, -0.625f},
			      { 0.625f, -0.375f}, { 0.375f, -0.125f}},
		.clipped   = {{ 0.125f, -0.375f}, { 0.250f, -0.500f},
			      { 0.500f, -0.500f}, { 0.500f, -0.250f},
			      { 0.375f, -0.125f}},
		.clipped_n = 5,
	},

	/* Box left edge largely intersects polygon right corner. */
	{
		.aligned   = false,
		.box       = BOX(-0.5f, -0.5f, 0.5f, 0.5f),
		.polygon   = {{-0.625f,  0.000f}, {-0.375f, -0.250f},
			      {-0.125f,  0.000f}, {-0.375f,  0.250f}},
		.clipped   = {{-0.500f,  0.125f}, {-0.500f, -0.125f},
			      {-0.375f, -0.250f}, {-0.125f,  0.000f},
			      {-0.375f,  0.250f}},
		.clipped_n = 5,
	},

	/* Box right edge largely intersects polygon left corner. */
	{
		.aligned   = false,
		.box       = BOX(-0.5f, -0.5f, 0.5f, 0.5f),
		.polygon   = {{ 0.125f,  0.000f}, { 0.375f, -0.250f},
			      { 0.625f,  0.000f}, { 0.375f,  0.250f}},
		.clipped   = {{ 0.125f,  0.000f}, { 0.375f, -0.250f},
			      { 0.500f, -0.125f}, { 0.500f,  0.125f},
			      { 0.375f,  0.250f}},
		.clipped_n = 5,
	},

	/* Box bottom/left corner largely intersects polygon top/right edge. */
	{
		.aligned   = false,
		.box       = BOX(-0.5f, -0.5f, 0.5f, 0.5f),
		.polygon   = {{-0.625f,  0.375f}, {-0.375f,  0.125f},
			      {-0.125f,  0.375f}, {-0.375f,  0.625f}},
		.clipped   = {{-0.500f,  0.500f}, {-0.500f,  0.250f},
			      {-0.375f,  0.125f}, {-0.125f,  0.375f},
			      {-0.250f,  0.500f}},
		.clipped_n = 5,
	},

	/* Box bottom edge largely intersects polygon top corner. */
	{
		.aligned   = false,
		.box       = BOX(-0.5f, -0.5f, 0.5f, 0.5f),
		.polygon   = {{-0.250f,  0.375f}, { 0.000f,  0.125f},
			      { 0.250f,  0.375f}, { 0.000f,  0.625f}},
		.clipped   = {{-0.125f,  0.500f}, {-0.250f,  0.375f},
			      { 0.000f,  0.125f}, { 0.250f,  0.375f},
			      { 0.125f,  0.500f}},
		.clipped_n = 5,
	},

	/* Box bottom/right corner largely intersects polygon top/left edge. */
	{
		.aligned   = false,
		.box       = BOX(-0.5f, -0.5f, 0.5f, 0.5f),
		.polygon   = {{ 0.125f,  0.375f}, { 0.375f,  0.125f},
			      { 0.625f,  0.375f}, { 0.375f,  0.625f}},
		.clipped   = {{ 0.125f,  0.375f}, { 0.375f,  0.125f},
			      { 0.500f,  0.250f}, { 0.500f,  0.500f},
			      { 0.250f,  0.500f}},
		.clipped_n = 5,
	},

	/* Box intersects entire smaller aligned quad of different winding
	 * orders and first edge orientations: */

	/* Clockwise winding and top/left initial vertex. */
	{
		.aligned   = true,
		.box       = BOX(-0.50f, -0.50f,  0.50f,  0.50f),
		.polygon   = {{-0.25f, -0.25f}, { 0.25f, -0.25f},
			      { 0.25f,  0.25f}, {-0.25f,  0.25f}},
		.clipped   = {{-0.25f, -0.25f}, { 0.25f, -0.25f},
			      { 0.25f,  0.25f}, {-0.25f,  0.25f}},
		.clipped_n = 4,
	},

	/* Clockwise winding and top/right initial vertex. */
	{
		.aligned   = true,
		.box       = BOX(-0.50f, -0.50f,  0.50f,  0.50f),
		.polygon   = {{ 0.25f, -0.25f}, { 0.25f,  0.25f},
			      {-0.25f,  0.25f}, {-0.25f, -0.25f}},
		.clipped   = {{ 0.25f, -0.25f}, { 0.25f,  0.25f},
			      {-0.25f,  0.25f}, {-0.25f, -0.25f}},
		.clipped_n = 4,
	},

	/* Clockwise winding and bottom/right initial vertex. */
	{
		.aligned   = true,
		.box       = BOX(-0.50f, -0.50f,  0.50f,  0.50f),
		.polygon   = {{ 0.25f,  0.25f}, {-0.25f,  0.25f},
			      {-0.25f, -0.25f}, { 0.25f, -0.25f}},
		.clipped   = {{ 0.25f,  0.25f}, {-0.25f,  0.25f},
			      {-0.25f, -0.25f}, { 0.25f, -0.25f}},
		.clipped_n = 4,
	},

	/* Clockwise winding and bottom/left initial vertex. */
	{
		.aligned   = true,
		.box       = BOX(-0.50f, -0.50f,  0.50f,  0.50f),
		.polygon   = {{-0.25f,  0.25f}, {-0.25f, -0.25f},
			      { 0.25f, -0.25f}, { 0.25f,  0.25f}},
		.clipped   = {{-0.25f,  0.25f}, {-0.25f, -0.25f},
			      { 0.25f, -0.25f}, { 0.25f,  0.25f}},
		.clipped_n = 4,
	},

	/* Counter-clockwise winding and top/left initial vertex. */
	{
		.aligned   = true,
		.box       = BOX(-0.50f, -0.50f,  0.50f,  0.50f),
		.polygon   = {{-0.25f, -0.25f}, {-0.25f,  0.25f},
			      { 0.25f,  0.25f}, { 0.25f, -0.25f}},
		.clipped   = {{-0.25f, -0.25f}, {-0.25f,  0.25f},
			      { 0.25f,  0.25f}, { 0.25f, -0.25f}},
		.clipped_n = 4,
	},

	/* Counter-clockwise winding and top/right initial vertex. */
	{
		.aligned   = true,
		.box       = BOX(-0.50f, -0.50f,  0.50f,  0.50f),
		.polygon   = {{ 0.25f, -0.25f}, {-0.25f, -0.25f},
			      {-0.25f,  0.25f}, { 0.25f,  0.25f}},
		.clipped   = {{ 0.25f, -0.25f}, {-0.25f, -0.25f},
			      {-0.25f,  0.25f}, { 0.25f,  0.25f}},
		.clipped_n = 4,
	},

	/* Counter-clockwise winding and bottom/right initial vertex. */
	{
		.aligned   = true,
		.box       = BOX(-0.50f, -0.50f,  0.50f,  0.50f),
		.polygon   = {{ 0.25f,  0.25f}, { 0.25f, -0.25f},
			      {-0.25f, -0.25f}, {-0.25f,  0.25f}},
		.clipped   = {{ 0.25f,  0.25f}, { 0.25f, -0.25f},
			      {-0.25f, -0.25f}, {-0.25f,  0.25f}},
		.clipped_n = 4,
	},

	/* Counter-clockwise winding and bottom/left initial vertex. */
	{
		.aligned   = true,
		.box       = BOX(-0.50f, -0.50f,  0.50f,  0.50f),
		.polygon   = {{-0.25f,  0.25f}, { 0.25f,  0.25f},
			      { 0.25f, -0.25f}, {-0.25f, -0.25f}},
		.clipped   = {{-0.25f,  0.25f}, { 0.25f,  0.25f},
			      { 0.25f, -0.25f}, {-0.25f, -0.25f}},
		.clipped_n = 4,
	},

	/* General purpose clipper tests: */

	/* Quad inside box. */
	{
		.aligned   = false,
		.box       = BOX (50.0f, 50.0f, 100.0f, 100.0f),
		.polygon   = QUAD(51.0f, 51.0f,  99.0f,  99.0f),
		.clipped   = QUAD(51.0f, 51.0f,  99.0f,  99.0f),
		.clipped_n = 4,
	},

	/* Quad bottom edge outside of box. */
	{
		.aligned   = false,
		.box       = BOX (50.0f, 50.0f, 100.0f, 100.0f),
		.polygon   = QUAD(51.0f, 51.0f,  99.0f, 101.0f),
		.clipped   = QUAD(51.0f, 51.0f,  99.0f, 100.0f),
		.clipped_n = 4,
	},

	/* Quad top edge outside of box. */
	{
		.aligned   = false,
		.box       = BOX (50.0f, 50.0f, 100.0f, 100.0f),
		.polygon   = QUAD(51.0f, 49.0f,  99.0f,  99.0f),
		.clipped   = QUAD(51.0f, 50.0f,  99.0f,  99.0f),
		.clipped_n = 4,
	},

	/* Quad left edge outside of box. */
	{
		.aligned   = false,
		.box       = BOX (50.0f, 50.0f, 100.0f, 100.0f),
		.polygon   = QUAD(49.0f, 51.0f,  99.0f,  99.0f),
		.clipped   = QUAD(50.0f, 51.0f,  99.0f,  99.0f),
		.clipped_n = 4,
	},

	/* Quad right edge outside of box. */
	{
		.aligned   = false,
		.box       = BOX (50.0f, 50.0f, 100.0f, 100.0f),
		.polygon   = QUAD(51.0f, 51.0f, 101.0f,  99.0f),
		.clipped   = QUAD(51.0f, 51.0f, 100.0f,  99.0f),
		.clipped_n = 4,
	},

	/* Rotated quad with edges adjacent to box corners. */
	{
		.aligned   = false,
		.box       = BOX(50.0f, 50.0f, 100.0f, 100.0f),
		.polygon   = {{ 25.0f, 75.0f}, {75.0f,  25.0f},
			      {125.0f, 75.0f}, {75.0f, 125.0f}},
		.clipped   = QUAD(50.0f, 50.0f, 100.0f, 100.0f),
		.clipped_n = 4,
	},

	/* Rotated quad with edges cutting out box corners. */
	{
		.aligned   = false,
		.box       = BOX(50.0f, 50.0f, 100.0f, 100.0f),
		.polygon   = {{ 37.5f,  75.0f}, { 75.0f,  37.5f},
			      {112.5f,  75.0f}, { 75.0f, 112.5f}},
		.clipped   = {{ 62.5f,  50.0f}, { 87.5f,  50.0f},
			      {100.0f,  62.5f}, {100.0f,  87.5f},
			      { 87.5f, 100.0f}, { 62.5f, 100.0f},
			      { 50.0f,  87.5f}, { 50.0f,  62.5f}},
		.clipped_n = 8,
	},

	/* Same as above using counter-clockwise winding. */
	{
		.aligned   = false,
		.box       = BOX(50.0f, 50.0f, 100.0f, 100.0f),
		.polygon   = {{ 37.5f,  75.0f}, { 75.0f, 112.5f},
			      {112.5f,  75.0f}, { 75.0f,  37.5f}},
		.clipped   = {{ 62.5f,  50.0f}, { 50.0f,  62.5f},
			      { 50.0f,  87.5f}, { 62.5f, 100.0f},
			      { 87.5f, 100.0f}, {100.0f,  87.5f},
			      {100.0f,  62.5f}, { 87.5f,  50.0f}},
		.clipped_n = 8,
	},

	/* Miscellaneous cases: */

	/* Box intersects entire same size aligned quad. */
	{
		.aligned   = true,
		.box       = BOX (-0.5f, -0.5f,  0.5f,  0.5f),
		.polygon   = QUAD(-0.5f, -0.5f,  0.5f,  0.5f),
		.clipped   = QUAD(-0.5f, -0.5f,  0.5f,  0.5f),
		.clipped_n = 4,
	},

	/* Box intersects entire rotated quad. */
	{
		.aligned   = false,
		.box       = BOX(-0.5f, -0.5f, 0.5f, 0.5f),
		.polygon   = {{-0.5f,  0.0f}, { 0.0f, -0.5f},
			      { 0.5f,  0.0f}, { 0.0f,  0.5f}},
		.clipped   = {{-0.5f,  0.0f}, { 0.0f, -0.5f},
			      { 0.5f,  0.0f}, { 0.0f,  0.5f}},
		.clipped_n = 4,
	},

	/* Box intersects rotated quad cutting out the 4 corners. */
	{
		.aligned   = false,
		.box       = BOX(-0.5f, -0.5f, 0.5f, 0.5f),
		.polygon   = {{-0.75f,  0.00f}, { 0.00f, -0.75f},
			      { 0.75f,  0.00f}, { 0.00f,  0.75f}},
		.clipped   = {{-0.50f,  0.25f}, {-0.50f, -0.25f},
			      {-0.25f, -0.50f}, { 0.25f, -0.50f},
			      { 0.50f, -0.25f}, { 0.50f,  0.25f},
			      { 0.25f,  0.50f}, {-0.25f,  0.50f}},
		.clipped_n = 8,
	},
};

TEST_P(quad_clip_expected, quad_clip_expected_data)
{
	struct vertex_clip_test_data *tdata = data;
	struct clipper_vertex clipped[8];
	struct clipper_quad quad;
	int clipped_n;

	clipper_quad_init(&quad, tdata->polygon, tdata->aligned);
	clipped_n = clipper_quad_clip(&quad, tdata->box, clipped);

	assert_vertices(clipped, clipped_n, tdata->clipped, tdata->clipped_n);
}

/* clipper_quad_clip_box32() tests: */

static const struct vertex_clip_test_data quad_clip_box32_expected_data[] = {
	/* Box bottom/right corner intersects polygon top/left corner. */
	{
		.aligned   = true,
		.box32     = BOX32(-3,    -3,    -1,    -1),
		.polygon   = QUAD (-2.5f, -2.5f,  2.5f,  2.5f),
		.clipped   = QUAD (-2.5f, -2.5f, -1.0f, -1.0f),
		.clipped_n = 4,
	},

	/* Box bottom/left corner intersects polygon top/right corner. */
	{
		.aligned   = true,
		.box32     = BOX32( 1,    -3,     3,    -1),
		.polygon   = QUAD (-2.5f, -2.5f,  2.5f,  2.5f),
		.clipped   = QUAD ( 1.0f, -2.5f,  2.5f, -1.0f),
		.clipped_n = 4,
	},

	/* Box top/right corner intersects polygon bottom/left corner. */
	{
		.aligned   = true,
		.box32     = BOX32(-3,     1,    -1,     3),
		.polygon   = QUAD (-2.5f, -2.5f,  2.5f,  2.5f),
		.clipped   = QUAD (-2.5f,  1.0f, -1.0f,  2.5f),
		.clipped_n = 4,
	},

	/* Box top/left corner intersects polygon bottom/right corner. */
	{
		.aligned   = true,
		.box32     = BOX32( 1,     1,     3,     3),
		.polygon   = QUAD (-2.5f, -2.5f,  2.5f,  2.5f),
		.clipped   = QUAD ( 1.0f,  1.0f,  2.5f,  2.5f),
		.clipped_n = 4,
	},
};

TEST_P(quad_clip_box32_expected, quad_clip_box32_expected_data)
{
	struct vertex_clip_test_data *tdata = data;
	struct clipper_vertex clipped[8];
	struct clipper_quad quad;
	int clipped_n;

	clipper_quad_init(&quad, tdata->polygon, tdata->aligned);
	clipped_n = clipper_quad_clip_box32(&quad, &tdata->box32, clipped);

	assert_vertices(clipped, clipped_n, tdata->clipped, tdata->clipped_n);
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
