/*
 * Copyright 2024 Collabora, Ltd.
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

#include "id-number-allocator.h"
#include "shared/helpers.h"
#include "shared/xalloc.h"
#include "shared/weston-assert.h"

struct weston_idalloc {
        struct weston_compositor *compositor;

        /* Each value on this array is a bucket of size 32. Bit is 0 if the id
         * is available, 1 otherwise. */
        uint32_t *buckets;

        uint32_t num_buckets;
        uint32_t lowest_free_bucket;
};

/**
 * Creates a unique id allocator
 *
 * \param compositor The compositor
 * \return The unique id allocator
 */
WESTON_EXPORT_FOR_TESTS struct weston_idalloc *
weston_idalloc_create(struct weston_compositor *compositor)
{
        struct weston_idalloc *idalloc;

        idalloc = xzalloc(sizeof(*idalloc));

        idalloc->compositor = compositor;

        /* Start with 2 buckets. If necessary we increase that on demand. */
        idalloc->num_buckets = 2;
        idalloc->buckets = xzalloc(idalloc->num_buckets * sizeof(*idalloc->buckets));

        /* Let's reserve id 0 for errors. So start with id 0 already taken. Set
         * the first bit of the first bucket to 1. */
        idalloc->buckets[idalloc->lowest_free_bucket] = 1;

        return idalloc;
}

/**
 * Destroys a unique id allocator
 *
 * \param idalloc The unique id allocator to destroy
 */
WESTON_EXPORT_FOR_TESTS void
weston_idalloc_destroy(struct weston_idalloc *idalloc)
{
        /* Sanity check: id 0 should still be taken. */
        weston_assert_true(idalloc->compositor, idalloc->buckets[0] & 1);

        free(idalloc->buckets);
        free(idalloc);
}

static void
update_lowest_free_bucket(struct weston_idalloc *idalloc)
{
        uint32_t old_lowest_free_bucket = idalloc->lowest_free_bucket;
        uint32_t *bucket;
        unsigned int i;

        for (i = old_lowest_free_bucket; i < idalloc->num_buckets; i++) {
                bucket = &idalloc->buckets[i];

                /* Skip full bucket */
                if (*bucket == 0xffffffff)
                        continue;

                idalloc->lowest_free_bucket = i;
                return;
        }

        /* We didn't find any free bucket, so we need to add more buckets. The
         * first one (from the new added) will be the lowest free. */
        idalloc->lowest_free_bucket = idalloc->num_buckets;
        idalloc->num_buckets *= 2;
        idalloc->buckets = xrealloc(idalloc->buckets,
                                    idalloc->num_buckets * sizeof(*idalloc->buckets));
}

/**
 * Gets an id from unique id allocator
 *
 * \param idalloc The unique id allocator
 * \return The unique id
 */
WESTON_EXPORT_FOR_TESTS uint32_t
weston_idalloc_get_id(struct weston_idalloc *idalloc)
{
        uint32_t *bucket = &idalloc->buckets[idalloc->lowest_free_bucket];
        unsigned int i;
        uint32_t id;

        /* Sanity check: lowest free bucket should not be full. */
        weston_assert_uint32_neq(idalloc->compositor, *bucket, 0xffffffff);

        for (i = 0; i < 32; i++) {
                /* Id already used, skip it. */
                if ((*bucket >> i) & 1)
                        continue;

                /* Found free id, take it and set it to 1 on the bucket. */
                *bucket |= 1 << i;
                id = (32 * idalloc->lowest_free_bucket) + i;

                /* Bucket may become full... */
                if (*bucket == 0xffffffff)
                        update_lowest_free_bucket(idalloc);

                return id;
        }

        /* We need to find an available id. */
        weston_assert_not_reached(idalloc->compositor,
                                  "should be able to allocate unique id");
}

/**
 * Releases a id back to unique id allocator
 *
 * When an id from the unique id allocator will not be used anymore, users
 * should call this function so that this id can be advertised again by the id
 * allocator.
 *
 * \param idalloc The unique id allocator
 * \param id The id to release
 */
WESTON_EXPORT_FOR_TESTS void
weston_idalloc_put_id(struct weston_idalloc *idalloc, uint32_t id)
{
        uint32_t bucket_index = id / 32;
        uint32_t id_index_on_bucket = id % 32;
        uint32_t *bucket;

        /* Shouldn't try to release index 0, we never advertise this id to anyone. */
        weston_assert_uint32_neq(idalloc->compositor, id, 0);

        /* Bucket index should be lower than num_buckets. */
        weston_assert_uint32_lt(idalloc->compositor,
                                bucket_index, idalloc->num_buckets);

        bucket = &idalloc->buckets[bucket_index];

        /* Shouldn't try to release a free index. */
        weston_assert_true(idalloc->compositor,
                           (*bucket >> id_index_on_bucket) & 1);

        /* We now have an available index id on this bucket, so it may become
         * the lowest bucket. */
        if (bucket_index < idalloc->lowest_free_bucket)
                idalloc->lowest_free_bucket = bucket_index;

        /* Zero the bit on the bucket. */
        *bucket &= ~(1 << id_index_on_bucket);
}
