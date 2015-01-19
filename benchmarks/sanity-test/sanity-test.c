#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <pthread.h>
#include <stdlib.h>
#include  <stddef.h>
#include  <string.h>
#include  <ctype.h>
#include  <time.h>

#include "mm_thread.h"
#include "malloc.h"

/**
 * This file is just meant as a test - can the allocator allocate proper memory
 */
const unsigned long SBB_SIZE = 4096;
const size_t SB_STRUCT_SIZE = 40;
const size_t LB_STRUCT_SIZE = 24;


/**
 * Literally the simplest testcase.
 * malloc(some size)
 * Make sure new superblock is allocated
 * free malloc'ed memory
 * Make sure no superblocks malloced or freed
 */
void test_malloc_small(size_t size) {
    assert(size < SBB_SIZE / 2);
    void * top = mm_malloc(0);
    /*printf("Top is now at %lu\n", top);*/

    /*printf("Trying to allocate space for 16 byte...\n");*/
    void *addr = mm_malloc(size);
    if (addr == NULL) {
        fprintf(stderr, "FAILURE: Could not allocate space for 1B\n");
    }

    void *t2 = mm_malloc(0);
    assert(t2 == top + SBB_SIZE);

    mm_free(addr);
    assert(t2 == mm_malloc(0));
}

/**
 * Repeatedly allocate small amounts of memory, then free it
 * Do this very very many times, but make sure exactly 1 SB is ever allocated
 */
void test_repeat_malloc_free_small() {
    size_t size = 100; // not bigger than SBB_SIZE / 2
    unsigned int num_its = 1000;
    void * top = mm_malloc(0);
    void * t2;
    void * prev_addr = NULL;
    unsigned int i;
    for (i = 0; i < num_its; i++) {
        void * addr = mm_malloc(size);
        assert(addr != NULL);
        if (prev_addr != NULL) {
            assert (addr == prev_addr);
        }

        prev_addr = addr;

        t2 = mm_malloc(0);
        assert (t2 == top + SBB_SIZE);

        mm_free(addr);
        assert (mm_malloc(0) == t2);
    }
}

/**
 * Repeatedly allocate large amounts of memory, then free it
 * Do this very very many times, but make sure new space is always allocated
 * x here is ceil(size / SBB_SIZE) + 1
 */
void test_repeat_malloc_free_large(size_t size) {
    assert(size > SBB_SIZE / 2);
    unsigned int num_its = 1000;
    void * top = mm_malloc(0);
    void * t2;
    void * prev_addr = NULL;
    void * addr;
    unsigned int x = (size + LB_STRUCT_SIZE) / SBB_SIZE + 1;
    fprintf(stderr, "x is currently: %u\n", x);
    unsigned int i;

    for (i = 0; i < 2; i++) {
        addr = mm_malloc(size);
        assert(addr != NULL);
        //NOTE:
        //addr not the same because largeblocks are reclaimed into superblocks, so we have to reallocate new space for it.

        prev_addr = addr;

        t2 = mm_malloc(0);

        assert (t2 == top + (i+1) * x * SBB_SIZE);

        mm_free(addr);
        assert (mm_malloc(0) == t2);

    }
}

/**
 * Allocate largeblocks follows by superblocks. Only the space used by largeblock
 * should be allocated.
 */
void test_malloc_large_super(size_t size) {
    assert(size > SBB_SIZE / 2);
    void * top = mm_malloc(0);
    void * t2;
    void * prev_addr = NULL;
    unsigned int x = (size + LB_STRUCT_SIZE) / SBB_SIZE + 1;
    fprintf(stderr, "x is currently: %u\n", x);
    void * addr;

    addr = mm_malloc(size);
    assert(addr != NULL);
    t2 = mm_malloc(0);
    assert (t2 == top + x * SBB_SIZE);

    mm_free(addr);
    assert (t2 == top + x * SBB_SIZE);   

    addr = mm_malloc(16);
    assert(addr != NULL);
    t2 = mm_malloc(0);
    assert (t2 == top + x * SBB_SIZE);

    mm_free(addr);
    assert (t2 == top + x * SBB_SIZE);

}

/**
 * Allocate largeblocks follows by superblocks. 
 * Only the last few allocations should extend beyond the addr allocated for largeblock
 */
void test_malloc_large_super_many(size_t size) {
    assert(size > SBB_SIZE / 2);
    void * top = mm_malloc(0);
    void * t2;
    void * prev_addr = NULL;
    unsigned int x = (size + LB_STRUCT_SIZE) / SBB_SIZE + 1;
    fprintf(stderr, "x is currently: %u\n", x);
    void * addr;

    addr = mm_malloc(size);
    assert(addr != NULL);
    t2 = mm_malloc(0);
    assert (t2 == top + x * SBB_SIZE);
    mm_free(addr);
    assert (t2 == top + x * SBB_SIZE);   

    addr = mm_malloc(16);
    assert(addr != NULL);
    t2 = mm_malloc(0);
    assert (t2 == top + x * SBB_SIZE);

    addr = mm_malloc(32);
    assert(addr != NULL);
    t2 = mm_malloc(0);
    assert (t2 == top + x * SBB_SIZE);

    addr = mm_malloc(64);
    assert(addr != NULL);
    t2 = mm_malloc(0);
    assert (t2 == top + x * SBB_SIZE);

    /* here it should be allocating new pages */
    addr = mm_malloc(128);
    assert(addr != NULL);
    t2 = mm_malloc(0);
    assert (t2 == top + x * SBB_SIZE + SBB_SIZE);
}

/**
 * In this testcase, allocate a superblock's worth of data (minus metadata)
 * Make sure that only a single superblock allocated
 * Free the data
 * Make sure no new data created on freeing
 */
void test_malloc_small_many(size_t size) {
    // has to be > 40 and < 4K / 2
    assert(size >= 64 && size < SBB_SIZE / 2);
    void * top = mm_malloc(0);


    int num_allocs = SBB_SIZE / size - 1;
    printf("--------> Doing %d allocations of size %u\n", num_allocs, size);
    void **alloc_addrs  = (void **)malloc(sizeof(void *) *( num_allocs));
    void * t2;
    /*printf("Starting with top at %lu\n", top);*/
    int i;
    for (i = 0; i < num_allocs; i++) {

        /*printf("Trying to allocate space for %u byte...\n", size);*/
        void *addr = mm_malloc(size);
        if (addr == NULL) {
            fprintf(stderr, "FAILURE: Could not allocate space for %uB\n", size);
        }
        alloc_addrs[i] = addr;
        t2 = mm_malloc(0);
        /*printf("[%d] Top is at %lu after allocating another %uB\n", i, t2, size);*/
        //if (i != 0) {
        //    assert(t2 == top + SBB_SIZE);
        //}

        /*printf("Top is now at %lu\n", t2);*/
    }
 
    printf("-----> Done malloc, now freeing\n");

    for (i = 0; i < num_allocs; i++) {
        printf("f %d\n", i);
        mm_free(alloc_addrs[i]);
    }
    assert(t2 == mm_malloc(0));
    free(alloc_addrs);
}

void begin_testcase(const char * testcase_name) {
    printf("=========================== %s ===========================\n", testcase_name);
}

void end_testcase(const char * testcase_name) {
    printf("============================");
    int i;
    for(i = 0; testcase_name[i] != '\0'; i++) {
        printf("=");
    }
    printf("============================\n");
}

int main (int argc, char **argv) {
    assert (argc == 2);
    int TEST_CASE = atoi(argv[1]);
    printf("Running testcase %d\n", TEST_CASE);

    mm_init();

    switch (TEST_CASE) {
        case 1:
            begin_testcase("test_malloc_small");
            test_malloc_small(64);
            end_testcase("test_malloc_small");
            break;
        case 2:
            begin_testcase("test_malloc_small_many");
            test_malloc_small_many(64); 
            end_testcase("test_malloc_small_many");
            break;
        case 3:
            begin_testcase("test_repeat_malloc_free_small");
            test_repeat_malloc_free_small(); 
            end_testcase("test_repeat_malloc_free_small");
            break;
        case 4:
            begin_testcase("test_repeat_malloc_free_large");
            test_repeat_malloc_free_large(SBB_SIZE * 5 + 1);
            end_testcase("test_repeat_malloc_free_large");
            break;
        case 5:
            begin_testcase("test_malloc_large_super");
            test_malloc_large_super(SBB_SIZE * 5 + 1);
            end_testcase("test_malloc_large_super");
            break;
        case 6:
            begin_testcase("test_malloc_large_super");
            test_malloc_large_super_many(SBB_SIZE * 2 + 1);
            end_testcase("test_malloc_large_super");
            break;
    }


    return 0;
}
