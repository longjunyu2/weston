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
#ifndef _WESTON_VERTEX_CLIPPING_H
#define _WESTON_VERTEX_CLIPPING_H

#include <stddef.h>
#include <stdbool.h>
#include <pixman.h>

struct clipper_vertex {
	float x, y;
};

struct clipper_quad {
	struct clipper_vertex polygon[4];
	struct clipper_vertex bbox[2];    /* Valid if !axis_aligned. */
	bool axis_aligned;
};

/*
 * Initialize a 'quad' clipping context. 'polygon' points to an array of 4
 * vertices defining a convex quadrilateral of any winding order. Call
 * 'clipper_quad_clip()' to clip an initialized 'quad' to a clipping box.
 * Clipping is faster if 'polygon' is an axis-aligned rectangle with edges
 * parallel to the axes of the coordinate space. 'axis_aligned' indicates
 * whether 'polygon' respects the conditions above.
 */
void
clipper_quad_init(struct clipper_quad *quad,
		  const struct clipper_vertex polygon[4],
		  bool axis_aligned);

/*
 * Compute the boundary vertices of the intersection of a convex quadrilateral
 * stored into a 'quad' clipping context and a clipping 'box'. 'box' points to
 * an array of 2 vertices where the values of the 1st vertex are less than or
 * equal to the values of the 2nd vertex. Either 0 or [3, 8] resulting vertices,
 * with the same winding order than the 'polygon' passed to
 * 'clipper_quad_init()', are written to 'vertices'. The return value is the
 * number of vertices created.
 */
int
clipper_quad_clip(struct clipper_quad *quad,
		  const struct clipper_vertex box[2],
		  struct clipper_vertex *restrict vertices);

/*
 * Utility function calling 'clipper_quad_clip()' but taking a pixman_box32
 * pointer as clipping box.
 */
int
clipper_quad_clip_box32(struct clipper_quad *quad,
			const struct pixman_box32 *box,
			struct clipper_vertex *restrict vertices);

float
clipper_float_difference(float a, float b);

#endif
