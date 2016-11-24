/*
    Copyright (c) 2016 Ioannis Charalampidis  All rights reserved.

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

#include <stdlib.h>

#include "../src/nn.h"
#include "../src/bus.h"
#include "testutil.h"

#include "../src/utils/alloc.h"
#include "../src/utils/chunk.h"
#include "../src/utils/chunkref.h"

#define TESTDATA_SIZE 1048576

static void nn_chunk_test_free_fn (void *p, void *user)
{
    *((int*)user) = 1;
    nn_free(p);
}

int main ()
{
    int i, free_called;
    size_t sz;
    void *garbage, *data;
    void *chunk1, *chunk2, *chunk3;
    uint8_t *test_data;
    struct nn_chunkref chunkref1, chunkref2, chunkref3;

    /* Initialize nn_alloc */
    nn_alloc_init();

    /* Generate some testing data */
    test_data = nn_alloc( TESTDATA_SIZE, "test_data" );
    nn_assert( test_data );
    for (i=0; i<TESTDATA_SIZE; i++) {
        test_data[i] = i % 0xFF;
    }


    /**
     * TEST 1 : Chunkref tests with small data on heap
     */

    /* Initialize chunkref1 */
    sz = NN_CHUNKREF_MAX-1;
    nn_chunkref_init( &chunkref1, sz );

    /* Copy some data on it */
    memcpy( nn_chunkref_data(&chunkref1), test_data, sz );

    /* Test copy and move */
    nn_chunkref_cp( &chunkref2, &chunkref1 );
    nn_chunkref_mv( &chunkref3, &chunkref1 );

    /* Chunkref1 should be now un-initialized */
    nn_assert( nn_chunkref_size(&chunkref1) == 0 );

    /* Compare data from the copied chunk */
    nn_assert( memcmp(
            nn_chunkref_data(&chunkref2),
            nn_chunkref_data(&chunkref3)
        ,sz ) == 0 );

    /* Get two chunks from same chunk and make sure it copies
       (heap-copied due to their size) */
    chunk1 = nn_chunkref_getchunk(&chunkref2);
    chunk2 = nn_chunkref_getchunk(&chunkref2);
    nn_assert( nn_chunk_deref(chunk1) != nn_chunk_deref(chunk2) );
    nn_assert( memcmp(nn_chunk_deref(chunk1), nn_chunk_deref(chunk2), sz) == 0);
    nn_chunk_free( chunk1 ); 
    nn_chunk_free( chunk2 ); 

    /* This also works across chunkrefs */
    chunk1 = nn_chunkref_getchunk(&chunkref2);
    chunk2 = nn_chunkref_getchunk(&chunkref3);
    nn_assert( nn_chunk_deref(chunk1) != nn_chunk_deref(chunk2) );
    nn_assert( memcmp(nn_chunk_deref(chunk1), nn_chunk_deref(chunk2), sz) == 0);
    nn_chunk_free( chunk1 ); 
    nn_chunk_free( chunk2 );

    /* Everything looks good */
    nn_chunkref_term( &chunkref2 );
    nn_chunkref_term( &chunkref3 );


    /**
     * TEST 2 : Chunkref with custom chunks
     */

    /* Allocate a garbage only to make nn_free valid */
    garbage = nn_alloc( 0, "garbage" );
    nn_assert( garbage );

    /* Run multiple iterations in order to expose all cases */
    for (i=0; i<10000; i++) {
        /* This along with the last function in this loop adds some
           noise in the heap. */
        nn_free( garbage );
        /* ========================================== */

        /* Create a chunk */
        sz = (rand() % (TESTDATA_SIZE-NN_CHUNKREF_MAX)) + NN_CHUNKREF_MAX;
        nn_assert( nn_chunk_alloc( sz, 0, &chunk1  ) == 0 );
        memcpy( chunk1, test_data, sz );

        /* Init one chunkref with existing chunk and create 2 empty */
        nn_chunkref_init_chunk( &chunkref1, chunk1 );
        nn_chunkref_init( &chunkref2, 0 );
        nn_chunkref_init( &chunkref3, 0 );

        /* Make sure chunkref data are correct */
        nn_assert( memcmp( nn_chunkref_data(&chunkref1), 
                           nn_chunk_deref(chunk1), sz ) == 0 );

        /* Move and copy */
        nn_chunkref_cp( &chunkref2, &chunkref1 );
        nn_chunkref_mv( &chunkref3, &chunkref1 );

        /* Make sure chunkref1 is gone */
        nn_assert( nn_chunkref_size(&chunkref1) == 0 );

        /* Compare data of chunkref2 and chunkref3 */
        nn_assert( memcmp(
                nn_chunkref_data(&chunkref2),
                nn_chunkref_data(&chunkref3)
            ,sz ) == 0 );

        /* Get chunk (should not dispose chunk) */
        chunk2 = nn_chunkref_getchunk( &chunkref2 );
        nn_assert( nn_chunkref_size(&chunkref2) != 0 );

        /* Pop chunk from chunkref1 (should now dispose chunk) */
        chunk3 = nn_chunkref_popchunk( &chunkref2 );
        nn_assert( nn_chunkref_size(&chunkref2) == 0 );

        /* Make sure the two chunks are the same */
        nn_assert( chunk2 == chunk3 );
        /* And equal to the one originally allocated */
        nn_assert( chunk1 == chunk1 );

        /* Free resources */
        nn_chunkref_term( &chunkref1 );
        nn_chunkref_term( &chunkref2 );
        nn_chunkref_term( &chunkref3 );
        nn_chunk_free( chunk1 );

        /* ========================================== */
        /* This function will add some noise to the heap */
        garbage = nn_alloc( sizeof(void*) * (rand() % 1024), "garbage" );
        nn_assert( garbage );
    }


    /**
     * TEST 3 : Chunkref with custom data
     */

    /* Allocate a garbage only to make nn_free valid */
    garbage = nn_alloc( 0, "garbage" );
    nn_assert( garbage );

    /* Run multiple iterations in order to expose all cases */
    for (i=0; i<10000; i++) {
        /* This along with the last function in this loop adds some
           noise in the heap. */
        nn_free( garbage );
        /* ========================================== */

        /* Create some data */
        sz = (rand() % (TESTDATA_SIZE-NN_CHUNKREF_MAX)) + NN_CHUNKREF_MAX;
        data = nn_alloc( sz, "data" );
        nn_assert( data );
        memcpy( data, test_data, sz );

        /* Create chunk from existing data */
        free_called = 0;
        nn_assert( nn_chunk_alloc_ptr( data, sz, &nn_chunk_test_free_fn,
            &free_called, &chunk1 ) == 0 );

        /* Make sure the chunk dereferences to the data pointer */
        nn_assert( nn_chunk_deref(chunk1) == data );

        /* Init one chunkref with existing chunk and create 2 empty */
        /* Init one chunkref with existing chunk and create 2 empty */
        nn_chunkref_init_chunk( &chunkref1, chunk1 );
        nn_chunkref_init( &chunkref2, 0 );
        nn_chunkref_init( &chunkref3, 0 );

        /* Make sure chunkref data are correct */
        nn_assert( memcmp( nn_chunkref_data(&chunkref1), 
                           nn_chunk_deref(chunk1), sz ) == 0 );

        /* Move and copy */
        nn_chunkref_cp( &chunkref2, &chunkref1 );
        nn_chunkref_mv( &chunkref3, &chunkref1 );

        /* Make sure chunkref1 is gone */
        nn_assert( nn_chunkref_size(&chunkref1) == 0 );

        /* Compare data of chunkref2 and chunkref3 */
        nn_assert( memcmp(
                nn_chunkref_data(&chunkref2),
                nn_chunkref_data(&chunkref3)
            ,sz ) == 0 );

        /* Get chunk (should not dispose chunk) */
        chunk2 = nn_chunkref_getchunk( &chunkref2 );
        nn_assert( nn_chunkref_size(&chunkref2) != 0 );

        /* Pop chunk from chunkref1 (should now dispose chunk) */
        chunk3 = nn_chunkref_popchunk( &chunkref2 );
        nn_assert( nn_chunkref_size(&chunkref2) == 0 );

        /* Make sure the two chunks are the same */
        nn_assert( chunk2 == chunk3 );
        /* And equal to the one originally allocated */
        nn_assert( chunk1 == chunk1 );

        /* Free resources */
        nn_chunkref_term( &chunkref1 );
        nn_chunkref_term( &chunkref2 );
        nn_chunkref_term( &chunkref3 );
        nn_chunk_free( chunk1 );

        /* Make sure free is called */
        nn_assert( free_called );

        /* ========================================== */
        /* This function will add some noise to the heap */
        garbage = nn_alloc( sizeof(void*) * (rand() % 1024), "garbage" );
        nn_assert( garbage );
    }

    /* Clean-up */
    nn_alloc_term();
    nn_free(garbage);

    return 0;
}

