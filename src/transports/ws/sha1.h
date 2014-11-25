/*
    Copyright (c) 2014 Wirebird Labs LLC.  All rights reserved.

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom
    the Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included
    in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
    IN THE SOFTWARE.
*/

#ifndef NN_SHA1_INCLUDED
#define NN_SHA1_INCLUDED

#include "../../utils/int.h"

#include <stddef.h>

/*****************************************************************************/
/*  SHA-1 SECURITY NOTICE:                                                   */
/*  The algorithm as designed below is not intended for general purpose use. */
/*  As-designed, it is a single-purpose function for this WebSocket          */
/*  Opening Handshake. As per RFC 6455 10.8, SHA-1 usage "doesn't depend on  */
/*  any security properties of SHA-1, such as collision resistance or        */
/*  resistance to the second pre-image attack (as described in [RFC4270])".  */
/*  Caveat emptor for uses of this function elsewhere.                       */
/*                                                                           */
/*  Based on sha1.c (Public Domain) by Steve Reid, these functions calculate */
/*  the SHA1 hash of arbitrary byte locations byte-by-byte.                  */
/*****************************************************************************/

#define NN_SHA1_RESULT_LEN 20
#define NN_SHA1_BLOCK_LEN 64

struct nn_sha1 {
    uint32_t buffer [NN_SHA1_BLOCK_LEN / sizeof (uint32_t)];
    uint32_t state [NN_SHA1_RESULT_LEN / sizeof (uint32_t)];
    uint32_t bytes_hashed;
    uint8_t buffer_offset;
    uint8_t is_little_endian;
};

void nn_sha1_init (struct nn_sha1 *self);
void nn_sha1_hash (struct nn_sha1 *self, const uint8_t *data, size_t len);
uint8_t* nn_sha1_result (struct nn_sha1 *self);

#endif

