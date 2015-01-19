#define _GNU_SOURCE

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include <assert.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sched.h>

#include "memlib.h"
#include "malloc.h"
#include "mm_thread.h"

// when we see memory which is full of this, we know we have a problem
// doubles for wiping out no-longer-used data structs
// note that EMPTY_MEM_CHAR = 0x23
#define EMPTY_MEM_CHAR      '#'

// DEBUGGING FLAGS
#define DB_MALLOC           2
#define DB_FREE             4
#define DB_MAKE_FREENODE    8
#define DB_MAKE_SUPERBLOCK  16
#define DB_CLEAR_SUPERBLOCK 32
#define DB_GET_SB           64
#define DB_INIT             128
#define DB_FIND_VITIM       256
#define DB_FIND_FREESEG     512
#define DB_MALLOC_TOPLVL    1024
#define DB_FREE_LARGE       2048
#define DB_LARGELIST        4096
#define DB_RECLAIM          8192
#define DB_FLAGS            (0)
//#define DB_FLAGS            (DB_FREE | DB_MALLOC | DB_INIT | DB_RECLAIM | DB_LARGELIST)
// DB_FREE | DB_MALLOC | DB_MAKE_FREENODE | DB_MALLOC_TOPLVL | DB_RECLAIM | DB_LARGELIST

// figs to toggle features
#define TOGGLE_MOVE_SMALLEST_TO_FRONT       1
#define FEATURES                            (0)

// unfortunately must explicitly keep as long :(
const unsigned long SB_SIZE = 4096;
const double F_thresh = 0.25;
const unsigned int K_thresh = 1;

// segment stuff. make sure that this stuff all matches up
// no segment is greater than half the SB_SIZE
const unsigned int SEG_SIZES[] = {64, 128, 256, 512, 1024, 2048};
#define NUM_SEGS            6

// debugging macro, for sanity

#define DEBUG(d, ...) do { if (d & DB_FLAGS) printf(__VA_ARGS__); } while (0)


/**
 * A node in the freelist linked list
 */
struct freelist_node {
    struct freelist_node* next;
};

struct freelist_node* make_freelist_node (void * begin) {
    struct freelist_node* node = (struct freelist_node *) begin;
    DEBUG(DB_MAKE_FREENODE, "[make_freelist_node] Creating freelist node at address %p\n", begin);

    node->next = NULL;
    return node;
}

/**
 * A superblock is the base unit of the allocator
 * They are all of the same size
 * All superblocks in a sequence have the same segment size
 */
struct superblock {
    unsigned int free_count;
    unsigned int seg_size;
    unsigned int heap_idx;
    unsigned int max_segs;
    struct freelist_node * freelist;
    struct superblock* next;
    struct superblock* prev;
    bool reclaimed;
};

struct largeblock {
    unsigned int size;
    struct largeblock* next;
    struct largeblock* prev;
};

struct sb_heap {
    struct superblock* sb_map[NUM_SEGS];
    struct superblock* sb_freelist;
    pthread_mutex_t heap_lock;
    unsigned int cur_f;
    unsigned int total_f;
    unsigned int cur_k;
};

// TOP - 1 is the largest allocated address
static void * TOP = NULL;
// BOTTOM is the lowest address of any superblock
static void * BOTTOM = NULL;

// indices in SEG_SIZES
static struct largeblock* large_list;
static pthread_mutex_t large_lock;
static pthread_mutex_t sbrk_lock;
static struct sb_heap* heap;


/**
 * Given an address, return the pointer to the containing superblock
 */
struct superblock * get_sb(void * addr) {
    DEBUG(DB_GET_SB, "[get_sb] Finding address of superblock for address %p\n", addr);

    unsigned int sb_idx = (addr - BOTTOM) / SB_SIZE;
    DEBUG(DB_GET_SB, "[get_sb] Index of superblock is %u\n", sb_idx);

    return ((struct superblock *) (BOTTOM + sb_idx * SB_SIZE));
}

/**
 * Given segment size, return index in array SEG_SIZES
 */
unsigned int get_seg_index (const unsigned int seg_size)
{
    /*printf("%u -> %u\n", seg_size, __builtin_clz(seg_size));*/
    // 27 = 32 - 5, where 32 is the size of unsigned int
    /*return 27 - __builtin_clz(seg_size);*/
    unsigned int i;
    for (i = 0; i < NUM_SEGS; i++) {
        if (SEG_SIZES[i] == seg_size) return i;
    }
    assert(false); // should never get here
    return 0; // to suppress compiler warning
}

/**
 * Return the number of segments that the header data in SB occupies
 */
int get_sb_header_seg_size(struct superblock * sb)
{
    return (sizeof (struct superblock) / sb->seg_size) + 1;
}

void clear_superblock (struct superblock* sb, unsigned int segment_size)
{
    DEBUG(DB_CLEAR_SUPERBLOCK, "[clear_superblock] Clearing SB at address %p\n", sb);
    DEBUG(DB_CLEAR_SUPERBLOCK, "[clear_superblock] Size of superblock struct is %lu\n",
          sizeof (struct superblock));

    struct freelist_node* fl_node;
    sb->free_count = 0;
    DEBUG(DB_CLEAR_SUPERBLOCK, "[clear_superblock] Setting reclaimed = false\n");
    sb->reclaimed = false;

    sb->freelist = NULL;
    /*if (sb->seg_size != 0) {*/
    /*free all the nodes in the superblock's freelist*/
    /*DEBUG(DB_CLEAR_SUPERBLOCK, "[clear_superblock] Deleting old superblock freelist\n");*/
    /*}*/

    DEBUG(DB_CLEAR_SUPERBLOCK, "[clear_superblock] Setting seg_size = %u\n", segment_size);
    sb->seg_size = segment_size;

    DEBUG(DB_CLEAR_SUPERBLOCK, "[clear_superblock] Creating superblock freelist\n");
    // used to update next pointers
    struct freelist_node* prev = NULL;

    // determine how many segments the superblock struct occupies
    int sb_offset = get_sb_header_seg_size(sb);
    DEBUG(DB_CLEAR_SUPERBLOCK, "[clear_superblock] Using %d segments for header data\n", sb_offset);

    int max_segs = SB_SIZE / segment_size;
    DEBUG(DB_CLEAR_SUPERBLOCK, "[clear_superblock] There should be %d segments in freelist\n", max_segs - sb_offset);
    //max_segs = max_segs - SB_SIZE / segment_size / 64;
    DEBUG(DB_CLEAR_SUPERBLOCK, "[clear_superblock] There should actually be %d segments in freelist\n", max_segs - sb_offset);
    sb->max_segs = max_segs - sb_offset;
    // the first segment is reserved for superblock struct
    int i;
    for (i = sb_offset; i < max_segs; i++) {
        void * node_addr = ((void *) sb) + ((unsigned long) i * segment_size);
        DEBUG(DB_CLEAR_SUPERBLOCK, "[clear_superblock] Created freelist node at address %p with segment size %u\n", node_addr, segment_size);
        fl_node = make_freelist_node(node_addr);
        sb->free_count++;
        DEBUG(DB_CLEAR_SUPERBLOCK, "[clear_superblock] Adding freelist node to freelist\n");
        if (sb->freelist == NULL)
            sb->freelist = fl_node;
        if (prev)
            prev->next = fl_node;
        prev = fl_node;
    }

    DEBUG(DB_CLEAR_SUPERBLOCK, "[clear_superblock] free_count=%u\n", sb->free_count);
}

struct superblock * make_superblock(const int segment_size) {
    pthread_mutex_lock(&sbrk_lock);
    void * begin = TOP;
    assert(segment_size > 0);
    assert(begin != NULL);

    DEBUG(DB_MAKE_SUPERBLOCK, "[make_superblock] Trying to increase TOP by %lu\n", SB_SIZE);
    void * addr = mem_sbrk(SB_SIZE);

    if (TOP != NULL && addr == NULL) {
        fprintf(stderr, "[make_superblock] mem_sbrk failed, ran out of memory\n");
        pthread_mutex_unlock(&sbrk_lock);
        return NULL;
    } else {
        TOP += SB_SIZE;

        if (BOTTOM == NULL) {
            BOTTOM = addr;
            DEBUG(DB_MAKE_SUPERBLOCK, "[make_superblock] This is the very first superblock. Setting BOTTOM = %p\n", BOTTOM);
            DEBUG(DB_MAKE_SUPERBLOCK, "[make_superblock] Amount of space used by the rest of our allocator is then %lu\n", BOTTOM - (void *) dseg_lo);
        }

        DEBUG(DB_MAKE_SUPERBLOCK, "[make_superblock] Success! Top now at %p\n", TOP);
        // clear out any crap that might be in here from before
        bzero(begin, SB_SIZE);
    }
    pthread_mutex_unlock(&sbrk_lock);

    // keep all superblock information in first segment of superblock
    struct superblock* sb = (struct superblock *) begin;
    DEBUG(DB_MAKE_SUPERBLOCK, "[make_superblock] Creating new SB at address %p with segment size %u\n", begin, segment_size);

    sb->seg_size = 0; // clear superblock needs this to check if this is a newly reclaimed guy
    sb->freelist = NULL;
    sb->next = NULL;
    sb->prev = NULL;
    sb->reclaimed = false;

    clear_superblock(sb, segment_size);
    return sb;
}

//large blocks for allocation > SB_SIZE/2
struct largeblock * make_largeblock(void * begin, const int allocation_size) {
    pthread_mutex_lock(&sbrk_lock);
    int target_alloc = (((allocation_size + sizeof(struct largeblock))/SB_SIZE + 1) * SB_SIZE);
    assert(target_alloc > 0);
    assert(target_alloc % SB_SIZE == 0);
    DEBUG(DB_MAKE_SUPERBLOCK, "[make_largeblock] Target allocation is %d\n", target_alloc);
    void * addr = mem_sbrk(target_alloc);

    if (TOP != NULL && addr == NULL) {
        fprintf(stderr, "[make_largeblock] mem_sbrk failed, ran out of memory\n");
        pthread_mutex_unlock(&sbrk_lock);
        return NULL;
    } else {
        TOP += target_alloc;

        if (BOTTOM == NULL) {
            BOTTOM = begin;
        }

        DEBUG(DB_MAKE_SUPERBLOCK, "[make_largeblock] Success! Top now at %p\n", TOP);
    }
    pthread_mutex_unlock(&sbrk_lock);
    DEBUG(DB_MAKE_SUPERBLOCK, "[make_largeblock] Creating largeblock at address %p spanning %lu superblocks\n", begin, target_alloc / SB_SIZE);

    struct largeblock* lb = (struct largeblock *) begin;
    lb->size = target_alloc/SB_SIZE;
    DEBUG(DB_MAKE_SUPERBLOCK, "[make_largeblock] Creating largeblock size is %d superblocks\n", lb->size);
    lb->next = NULL;
    lb->prev = NULL;

    //inserting into large list
    if (large_list == NULL) {
        DEBUG(DB_MAKE_SUPERBLOCK, "[make_largeblock] This is the first largeblock\n");
        large_list = lb;
    } else {
        DEBUG(DB_MAKE_SUPERBLOCK, "[make_largeblock] Adding newly created largeblock to front of large_list\n");
        struct largeblock * old_head = large_list;
        DEBUG(DB_MAKE_SUPERBLOCK, "[make_largeblock] before: old head next is: %p, prev is: %p\n", old_head->next, old_head->prev);
        lb->next = old_head;
        old_head->prev = lb;
        DEBUG(DB_MAKE_SUPERBLOCK, "[make_largeblock] after: old head next is: %p, prev is: %p\n", old_head->next, old_head->prev);
        large_list = lb;
        DEBUG(DB_MAKE_SUPERBLOCK, "[make_largeblock] after: large_list next is: %p, prev is: %p\n", large_list->next, large_list->prev);
    }
    return lb;
}

/**
 * Return address of the large block corresponding to the given address in large_list if found
 * Return NULL on failure
 */
struct largeblock * find_in_large_list(void * addr) {
    DEBUG(DB_LARGELIST, "[find_largeblock] searching for large block if exists\n");

    struct largeblock * cur_lb;
    for (cur_lb = large_list; cur_lb != NULL; cur_lb = cur_lb->next) {
        if ((void *)cur_lb + sizeof(struct largeblock) == addr) {
            DEBUG(DB_LARGELIST, "[find_largeblock] Found matching large block at address %p\n", cur_lb);
            return cur_lb;
        }
    }

    DEBUG(DB_LARGELIST, "[find_largeblock] Did not find address %p in large_list\n", addr);
    return NULL;
}

/**
 * Add an element to the front of the sb freelist
 */
void sb_freelist_push (struct superblock * sb, struct freelist_node * fl_node)
{
    assert(sb != NULL);
    assert(fl_node != NULL);

    fl_node->next = sb->freelist;
    sb->freelist = fl_node;
    sb->free_count++;
}

/**
 * Remove and return the first freelist node in the sb's freelist
 */
struct freelist_node * sb_freelist_pop (struct superblock * sb) {
    assert(sb != NULL);
    assert(sb->free_count > 0);
    assert(sb->freelist != NULL);

    DEBUG(DB_MALLOC, "[sb_freelist_pop] next is pointing to: %p\n", sb->freelist->next);
    struct freelist_node * node = sb->freelist;

    sb->freelist = sb->freelist->next;
    sb->free_count--;

    // the freelist should be not null whenever free_count != 0
    assert(sb->free_count == 0 || sb->freelist != NULL);

    DEBUG(DB_MALLOC, "[sb_freelist_pop] New free_count of SB is %u\n", sb->free_count);
    return node;
}

/**
 * Reclaim the given superblock
 * + Remove from chain of blocks with same segsize
 * + Set reclaimed = true
 */
void reclaim_superblock (struct superblock * sb, const int src_idx, const int dest_idx)
{
    assert(sb != NULL);
    assert(sb->free_count == sb->max_segs);

    if (sb->prev) sb->prev->next = sb->next;
    if (sb->next) sb->next->prev = sb->prev;
    int heap_idx = dest_idx;

    // update first pointer to linked list, if I was the first
    if (sb->prev == NULL) {
        unsigned int seg_idx = get_seg_index(sb->seg_size);
        //reclaim comes from sb_map, freelist, and free_large
        if (heap[src_idx].sb_map[seg_idx] == sb) { //check if it came from free_large
            heap[src_idx].sb_map[seg_idx] = sb->next;
        } else if (heap[src_idx].sb_freelist == sb ) {
            heap[src_idx].sb_freelist = sb->next;
        }
    }
    sb->reclaimed = true;
    sb->heap_idx = dest_idx;

    // add to list of reclaimed superblocks
    DEBUG(DB_FREE, "[mm_free] first element of freelist is %p\n", heap[heap_idx].sb_freelist);
    if (heap[heap_idx].sb_freelist == NULL) {
        // first reclaimed superblock
        DEBUG(DB_FREE, "[mm_free] This is the first reclaimed superblock for heap %d\n", heap_idx);
        sb->prev = NULL;
        sb->next = NULL;
        heap[heap_idx].sb_freelist = sb;
    } else {
        // add to front of reclaimed superblocks
        sb->prev = NULL;
        sb->next = heap[heap_idx].sb_freelist;
        if ( heap[heap_idx].sb_freelist )
            heap[heap_idx].sb_freelist->prev = sb;
        heap[heap_idx].sb_freelist = sb;
    }
}

//map to free taken care of
//free to map taken care of
//free to free

int getHeapBlocksCount(const int cpu_count)
{
    return (sizeof (struct sb_heap) * (cpu_count + 1) / SB_SIZE) + 1;
}

/**
 * calculate the ratio of how much space is used
 **/
float calculate_ratio(struct superblock * sb)
{
    return 1.0 * (sb->max_segs - sb->free_count) / sb->max_segs;
}

/* move/insert superblock to the front of the sb_map list */
void insert_sb_in_front(struct superblock * in_front, const int heap_idx, const int map_idx)
{
    // avoid loops
    if (heap[heap_idx].sb_map[map_idx] == in_front) return;
    assert (in_front != NULL);

    //isolate in front from list
    if (in_front->prev != NULL) in_front->prev->next = in_front->next;
    if (in_front->next != NULL) in_front->next->prev = in_front->prev;
    in_front->prev = NULL;
    in_front->next = NULL;

    struct superblock * current_front = heap[heap_idx].sb_map[map_idx];

    //add in_front to the front of target
    in_front->prev = NULL;
    in_front->next = current_front;
    if (current_front != NULL) {
        current_front->prev = in_front;
    }
    heap[heap_idx].sb_map[map_idx] = in_front;
}

void make_heap(const int heap_idx) {
    heap[heap_idx].sb_freelist = NULL;
    heap[heap_idx].cur_f = 0;
    heap[heap_idx].total_f = 0;
    heap[heap_idx].cur_k = 0;
    pthread_mutexattr_t attrs;
    pthread_mutexattr_init(&attrs);
    pthread_mutex_init(&(heap[heap_idx].heap_lock), &attrs);
}

////////////////////////////////// MAIN WORKHORSE FUNCTIONS ///////////////////////
int mm_init(void)
{
    if (dseg_lo == NULL && dseg_hi == NULL) {
        int result = mem_init();
        DEBUG(DB_INIT, "[mm_init] dseg_lo at %p and dseg_hi at %p\n", dseg_lo, dseg_hi);
        TOP = dseg_lo;

        DEBUG(DB_INIT, "[mm_init] init_TOP: %p\n", TOP);

        //initilize heap; dseg_hi initially dseg_lo-1;
        //global heap is heap 0;
        int cpu_count = getNumProcessors();
        int heap_blocks_count = getHeapBlocksCount(cpu_count);
        heap = mem_sbrk(heap_blocks_count * SB_SIZE) + 1;
        TOP += heap_blocks_count * SB_SIZE;
        int i, j;
        for (j = 0; j <= cpu_count; j++) {
            for (i = 0; i < NUM_SEGS; i++) {
                heap[j].sb_map[i] = NULL;
            }

            make_heap(j);
        }

        large_list = NULL;

        pthread_mutexattr_t large_attrs;
        pthread_mutexattr_init(&large_attrs);
        pthread_mutex_init(&(large_lock), &large_attrs);

        pthread_mutexattr_t sbrk_attrs;
        pthread_mutexattr_init(&sbrk_attrs);
        pthread_mutex_init(&(sbrk_lock), &sbrk_attrs);


        DEBUG(DB_INIT, "[mm_init] at end: %p\n", TOP);
        return result;
    }

    return 0;
}

/**
 * Return the segment size needed to store the given size
 * Return value IS THE INDEX IN SEG_SIZES
 * Do not call this function with size > MAX_SEG (= SEG_SIZES[NUM_SEGS - 1])
 */
unsigned int get_seg_size(const size_t size)
{
    assert (size <= SEG_SIZES[NUM_SEGS - 1]);
    int i;
    for (i = 0; i < NUM_SEGS; i++) {
        if (size <= SEG_SIZES[i]) {
            return i;
        }
    }

    // should always find matching seg size
    assert(false);
    return NUM_SEGS; // to suppress compiler warnings
}

unsigned int get_heap_index()
{
    return sched_getcpu() + 1;
}

/**
 * Look for a free node in the given heap
 * If found, pop from parent superblock
 * Return address of the free node found, return NULL on failure
 */
void * find_free_node (const unsigned int seg_size_idx, const unsigned int heap_idx)
{
    struct superblock * sb_list = heap[heap_idx].sb_map[seg_size_idx];
    void * addr;

    if (sb_list) {
        DEBUG(DB_MALLOC, "[small_malloc] Superblock list is non-empty\n");
        DEBUG(DB_MALLOC, "[small_malloc] Traversing through superblocks to find non-empty free list...\n");
    } else {
        DEBUG(DB_MALLOC, "[small_malloc] Superblock list is empty\n");
    }
    struct superblock * sb;
    for (sb = sb_list; sb != NULL; sb = sb->next) {
        assert((void *) sb < TOP);
        assert((void *) sb >= BOTTOM);
        assert(sb != sb->next);

        DEBUG(DB_FIND_FREESEG, "[small_malloc] Checking SB at address %p with seg_size %u for free space...\n", sb, sb->seg_size);
        if (sb->free_count > 0) {
            DEBUG(DB_MALLOC, "[small_malloc] Found non-empty freelist in superblock at %p\n", sb);
            DEBUG(DB_MALLOC, "[small_malloc] before entering pop, freelist: %p\n", sb->freelist);
            DEBUG(DB_MALLOC, "[small_malloc] before entering pop, free_count: %u\n", sb->free_count);

            struct freelist_node * fl_node = sb_freelist_pop(sb);
            assert (fl_node != NULL);

            addr = (void *) fl_node;
            // clear the node
            memset(addr, EMPTY_MEM_CHAR, sb->seg_size);
            heap[heap_idx].cur_f += 1;
            return addr;
        }
    }

    return NULL;
}

/**
 * Find a reclaimed superblock.
 * First look in own sb_freeelist, then in global heap's sb_freelist
 * Return NULL on failure
 */
void * find_reclaimed_superblock(const unsigned int heap_idx, const unsigned int seg_size)
{
    unsigned int seg_idx = get_seg_index(seg_size);
    if (heap[heap_idx].sb_freelist) {
        DEBUG(DB_MALLOC_TOPLVL, "[find_reclaimed_superblock] Found superblock %p in reclaimed superblocks for heap %u\n", heap[heap_idx].sb_freelist, heap_idx);
        DEBUG(DB_MALLOC, "[find_reclaimed_superblock] Removing superblock from reclaimed superblocks\n");
        struct superblock * new_sb = heap[heap_idx].sb_freelist;
        heap[heap_idx].sb_freelist = new_sb->next;
        if (new_sb->next)
            heap[heap_idx].sb_freelist->prev = NULL;

        DEBUG(DB_MALLOC, "[find_reclaimed_superblock] Clearing reclaimed superblock\n");
        clear_superblock(new_sb, seg_size);

        DEBUG(DB_MALLOC, "[find_reclaimed_superblock] Got back cleared superblock starting at %p, seg_size %u, free_count %u\n", new_sb, new_sb->seg_size, new_sb->free_count);

        DEBUG(DB_MALLOC, "[find_reclaimed_superblock] Clearing next and previous pointers for reclaimed superblock\n");
        new_sb->next = NULL;
        new_sb->prev = NULL;

        DEBUG(DB_MALLOC_TOPLVL, "[find_reclaimed_superblock] freelist now points to %p\n",heap[heap_idx].sb_freelist);
        return (void *) new_sb;
    } else if (heap[0].sb_freelist != NULL) {
        pthread_mutex_lock(&(heap[0].heap_lock));
        if (heap[0].sb_freelist != NULL) {
            DEBUG(DB_MALLOC, "[find_reclaimed_superblock] Found superblock in reclaimed superblocks in global heap\n");
            DEBUG(DB_MALLOC, "[find_reclaimed_superblock] Removing superblock from reclaimed superblocks\n");
            struct superblock * new_sb = heap[0].sb_freelist;
            heap[0].sb_freelist = new_sb->next;
            if (new_sb->next)
                heap[0].sb_freelist -> prev = NULL;

            DEBUG(DB_MALLOC, "[find_reclaimed_superblock] Clearing reclaimed superblock\n");
            clear_superblock(new_sb, seg_size);
            DEBUG(DB_MALLOC, "[find_reclaimed_superblock] Got back cleared superblock starting at %p, seg_size %u, free_count %u\n", new_sb, new_sb->seg_size, new_sb->free_count);
            DEBUG(DB_MALLOC, "[find_reclaimed_superblock] Clearing next and previous pointers for reclaimed superblock\n");
            new_sb->next = NULL;
            new_sb->prev = NULL;
            pthread_mutex_unlock(&(heap[0].heap_lock));
            return (void *) new_sb;
        } 
        pthread_mutex_unlock(&(heap[0].heap_lock));
    }
    return NULL;
}

/**
 * Allocate memory for sizes <= SB_SIZE / 2
 */
void * small_malloc(const size_t size)
{
    int heap_idx = get_heap_index();

    DEBUG(DB_MALLOC, "[small_malloc] malloc to heap index: %d\n", heap_idx);

    unsigned int seg_size_idx = get_seg_size(size);
    assert (seg_size_idx < NUM_SEGS);
    unsigned int seg_size = SEG_SIZES[seg_size_idx];

    DEBUG(DB_MALLOC, "[small_malloc] Finding superblock linked list with seg size %d\n", seg_size);

    // now, we know which sb_list we want
    // go through each superblock and see if there is anything in the freelists
    void * start_addr = NULL;
    DEBUG(DB_MALLOC_TOPLVL, "[small_malloc] about to enter FIND_FREE_NODE\n");
    pthread_mutex_lock(&(heap[heap_idx].heap_lock));
    start_addr = find_free_node(seg_size_idx, heap_idx);
    if (start_addr != NULL) {
        // found memory in freelist

        pthread_mutex_unlock(&(heap[heap_idx].heap_lock));
        return start_addr;
    }

    DEBUG(DB_MALLOC, "[small_malloc] No non-empty freelists found.\n");
    DEBUG(DB_MALLOC, "[small_malloc] Looking for superblock in reclaimed superblocks...\n");
    struct superblock* new_sb = find_reclaimed_superblock(heap_idx, seg_size);

    if (new_sb == NULL) {
        pthread_mutex_unlock(&(heap[heap_idx].heap_lock));
        DEBUG(DB_MALLOC, "[small_malloc] No reclaimed superblocks. Creating a new one.\n");
        new_sb = make_superblock(seg_size);

        if (new_sb == NULL) {
            DEBUG(DB_MALLOC, "[small_malloc] Error: failed to create a new superblock\n");
            return NULL;
        }
        pthread_mutex_lock(&(heap[heap_idx].heap_lock));
    } else {
        DEBUG(DB_MALLOC, "[small_malloc] Managed to reclaim superblock\n");
    }

    assert(new_sb != NULL);

    new_sb->heap_idx = heap_idx;
    heap[heap_idx].total_f += new_sb->max_segs;
    heap[heap_idx].cur_k += 1;

    DEBUG(DB_MALLOC, "[small_malloc] Adding newly created/reclaimed superblock to front of sb_list\n");
    DEBUG(DB_MALLOC, "[small_malloc] adding to heap with index: %d\n", heap_idx);

    insert_sb_in_front(new_sb, heap_idx, seg_size_idx);

    DEBUG(DB_MALLOC, "[small_malloc] Taking off node from new superblock's freelist...\n");
    DEBUG(DB_MALLOC, "[small_malloc] before entering pop, freelist: %p\n", new_sb->freelist);
    DEBUG(DB_MALLOC, "[small_malloc] before entering pop, free_count: %u\n", new_sb->free_count);

    assert(new_sb->freelist != NULL);
    struct freelist_node * fl_node = sb_freelist_pop(new_sb);

    heap[heap_idx].cur_f += 1;
    DEBUG(DB_MALLOC_TOPLVL, "[small_malloc] after pop, free_list points to: %p\n", new_sb->freelist);
    DEBUG(DB_MALLOC, "[small_malloc] Got freelist node with starting address %p and segment size %u\n", fl_node, new_sb->seg_size);
    pthread_mutex_unlock(&(heap[heap_idx].heap_lock));

    start_addr = (void *) fl_node;
    assert(start_addr != NULL);
    memset(start_addr, EMPTY_MEM_CHAR, new_sb->seg_size);
    DEBUG(DB_MALLOC, "[small_malloc] Returning address %p\n", start_addr);
    
    return start_addr;
}


/**
 * Simple multiplexer between mallocing small and large blocks
 */
void * mm_malloc(size_t size)
{
    if(BOTTOM && TOP) {
        assert(TOP > BOTTOM);
        assert((TOP - BOTTOM) % SB_SIZE == 0);
    }

    DEBUG(DB_MALLOC_TOPLVL, "[mm_malloc] Trying to allocate memory chunk of size %lu\n", size);

    if (size == 0) {
        // as a debugging measure, return TOP when size 0 requested
        return TOP;
    }

    if (size > SB_SIZE / 2) {
        DEBUG(DB_MALLOC_TOPLVL, "[mm_malloc] allocating for size greater than SB_SIZE/2\n");
        pthread_mutex_lock(&large_lock);
        struct largeblock * lb = make_largeblock(TOP, size);
        void * addr = (void *)lb + sizeof(struct largeblock);
        pthread_mutex_unlock(&large_lock);
        return addr;
    } else {
        DEBUG(DB_MALLOC_TOPLVL, "[mm_malloc] allocating for size <= SB_SIZE / 2\n");
        void * addr = small_malloc(size);
        DEBUG(DB_MALLOC_TOPLVL, "[mm_malloc] about to return address %p\n", addr);
        return addr;
    }
}

void init_largeblock_to_superblock(struct superblock *sb)
{
    sb->seg_size = 0; // clear superblock needs this to check if this is a newly reclaimed guy
    sb->freelist = NULL;
    sb->next = NULL;
    sb->prev = NULL;
    sb->reclaimed = true;
}


void free_large (void * addr, struct largeblock * lb)
{

    if (lb->prev != NULL)
        lb->prev->next = lb->next;
    if (lb->next != NULL)
        lb->next->prev = lb->prev;
    if (large_list == lb) {
        large_list = lb->next;
    }

    lb->next = NULL;
    lb->prev = NULL;

    //convert the freed space to superblocks and append to sb_freelist
    unsigned int span = lb->size;
    DEBUG(DB_FREE, "[free_large] lb size is: %u\n", lb->size);
    struct superblock * partition;
    pthread_mutex_lock(&(heap[0].heap_lock));
    int i;
    for (i = 0; i < span; i++) {
        partition = (struct superblock *) (((void*) lb) + (SB_SIZE * i));
        partition->seg_size = 0;
        init_largeblock_to_superblock(partition);
        clear_superblock(partition, 16);
        reclaim_superblock(partition, 0, 0);
    }
    pthread_mutex_unlock(&(heap[0].heap_lock));
}

/**
 * Examine this heap to check for SBs to move to heap 0
 * If the heap threshold is higher than F, do nothing.
 */
void maybe_move_to_heap_zero(const int heap_idx)
{
    float heapThreshold = 1.0 * heap[heap_idx].cur_f / heap[heap_idx].total_f;

    if (heapThreshold < F_thresh && heap[heap_idx].cur_k > K_thresh && heap_idx != 0) {
  
        if (heap[heap_idx].sb_freelist != NULL) {
            pthread_mutex_lock(&(heap[0].heap_lock));
            heap[heap_idx].cur_k--;
            reclaim_superblock(heap[heap_idx].sb_freelist, heap_idx, 0);
            pthread_mutex_unlock(&(heap[0].heap_lock));
        }
    }
}

void free_small (void * addr)
{
    assert(addr < TOP);
    assert(addr >= BOTTOM);

    struct superblock * sb = get_sb(addr);
    assert(sb != NULL);
    assert((void *)sb >= BOTTOM);
    assert((void *)sb < TOP);

    //a thread can free address currently on another heap
    int heap_idx = sb->heap_idx;

    pthread_mutex_lock(&(heap[heap_idx].heap_lock));

    DEBUG(DB_FREE, "[free_small] Heap index is %d\n", heap_idx);
    DEBUG(DB_FREE, "[free_small] Found relevant superblock. It has starting address %p and segment size %u\n", sb, sb->seg_size);

    // create a new freelist node for this segment
    struct freelist_node* node = make_freelist_node(addr);
    assert(node != NULL);
    unsigned int seg_idx = get_seg_index(sb->seg_size);
    assert((void *) node >= BOTTOM);
    assert((void *) node < TOP);

    DEBUG(DB_FREE, "[free_small] Created new freelist node at address %p of size %u\n", addr, sb->seg_size);
    // add the freelist node to front of superblock's freelist
    sb_freelist_push(sb, node);
    heap[heap_idx].cur_f--;

    if (sb->free_count == sb->max_segs) {
        heap[heap_idx].total_f -= sb->max_segs;
        heap[heap_idx].cur_f -= sb->max_segs - sb->free_count;
        reclaim_superblock(sb, heap_idx, heap_idx);
    } else {
        DEBUG(DB_FREE, "[free_small] Not reclaiming this block\n");
        DEBUG(DB_FREE, "[free_small] free_count=%u\n", sb->free_count);

        // this SB has some nodes in freelist, so move to front of sb_map
        // that way, should be very fast to find new freelist nodes
        insert_sb_in_front(sb, heap_idx, seg_idx);
    }

    maybe_move_to_heap_zero(heap_idx);

    pthread_mutex_unlock(&(heap[heap_idx].heap_lock));
}


void mm_free(void * addr)
{
    DEBUG(DB_FREE, "[mm_free] Trying to free memory starting at address %p\n", addr);
    if (addr == NULL) return;

    //if address is in large_list remove it and we're done for now
    // we need the large lock for find_in_large_list
    struct largeblock *lb = find_in_large_list(addr);
    if (lb != NULL) {
        pthread_mutex_lock(&large_lock);
        DEBUG(DB_FREE, "[mm_free] before going into free_large %p\n", lb);
        DEBUG(DB_FREE, "[mm_free] lb next is: %p, prev is: %p\n", lb->next, lb->prev);
        DEBUG(DB_FREE, "[mm_free] lb size is: %d\n", lb->size);
        if(lb != NULL) {
            free_large(addr, lb);
        }
        pthread_mutex_unlock(&large_lock);
    } else {
        // we no longer need the lock, since this is not a large list operation
        free_small(addr);
    }
}
