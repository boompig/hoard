#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include <assert.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/mman.h>

#include "memlib.h"
#include "malloc.h"
#include "mm_thread.h"


/**
 * The allocator written in C
 * Written in such a way as to be easily testable
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <limits.h>

// CONSTANTS
#define EMPTY_MEM_CHAR      '#'

// DEBUGGING FLAGS
#define DB_PRINT            1
#define DB_MALLOC           2
#define DB_FREE             4
#define DB_MAKE_FREENODE    8
#define DB_MAKE_SUPERBLOCK  16
#define DB_CLEAR_SUPERBLOCK 32
#define DB_GET_SB           64
#define DB_FLAGS            (DB_FREE | DB_GET_SB | DB_CLEAR_SUPERBLOCK)


// unfortunately must explicitly keep as long :(
const unsigned long SB_SIZE = 4096;

// segment stuff. make sure that this stuff all matches up
const unsigned int SEG_SIZES[] = {16, 32, 64, 128, 256, 512, 1024, 2048};
#define MIN_SEG_SIZE        16
#define MAX_SEG_SIZE        2048
#define NUM_SEGS            8

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
    DEBUG(DB_MAKE_FREENODE, "[make_freelist_node] Creating freelist node at address %lu\n", begin);

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

// TOP - 1 is the largest allocated address
static void * TOP;
// BOTTOM is the lowest address of any superblock
static void * BOTTOM;

// indices in SEG_SIZES
static struct superblock* sb_map[NUM_SEGS];
static struct superblock* sb_freelist;

struct superblock * get_sb(void * addr) {
    // TODO this will be dseg_lo in our real implementation
    // or actually will refer to the first address of the bottom-most superblock
    DEBUG(DB_GET_SB, "[get_sb] Finding address of superblock for address %lu\n", addr);

    unsigned int sb_idx = (addr - bottom) / SB_SIZE;
    DEBUG(DB_GET_SB, "[get_sb] Index of superblock is %u\n", sb_idx);

    return ((struct superblock *) (BOTTOM + sb_idx * SB_SIZE));
}

/**
 * Return the number of segments that the header data in SB occupies
 */
int get_sb_header_seg_size(struct superblock * sb) {
    return (sizeof (struct superblock) / sb->seg_size) + 1;
}

void clear_superblock (struct superblock* sb, unsigned int segment_size) {
    DEBUG(DB_CLEAR_SUPERBLOCK, "[clear_superblock] Clearing superblock at address %p\n", sb);
    DEBUG(DB_CLEAR_SUPERBLOCK, "[clear_superblock] Size of superblock struct is %lu\n",
            sizeof (struct superblock));

    struct freelist_node* fl_node;
    sb->free_count = 0;
    DEBUG(DB_CLEAR_SUPERBLOCK, "[clear_superblock] Setting reclaimed = false\n");
    sb->reclaimed = false;

    if (sb->seg_size != 0) {
        // free all the nodes in the superblock's freelist
        DEBUG(DB_CLEAR_SUPERBLOCK, "[clear_superblock] Deleting old superblock freelist\n");
        memset(sb + segment_size, EMPTY_MEM_CHAR, SB_SIZE - segment_size);
        sb->freelist = NULL;
    }

    DEBUG(DB_CLEAR_SUPERBLOCK, "[clear_superblock] Setting segsize to %d\n", segment_size);
    sb->seg_size = segment_size;

    DEBUG(DB_CLEAR_SUPERBLOCK, "[clear_superblock] Creating superblock freelist\n");
    // used to update next pointers
    struct freelist_node* prev = NULL;

    // determine how many segments the superblock struct occupies
    int sb_offset = get_sb_header_seg_size(sb);
    DEBUG(DB_CLEAR_SUPERBLOCK, "[clear_superblock] Using %d segments for header data\n", sb_offset);

    int max_segs = SB_SIZE / segment_size;
    DEBUG(DB_CLEAR_SUPERBLOCK, "[clear_superblock] There should be %d segments in freelist\n", max_segs - sb_offset);

    // the first segment is reserved for superblock struct
    for (int i = sb_offset; i < max_segs; i++) {
        void * node_addr = ((void *) sb) + ((unsigned long) i * segment_size);
        DEBUG(DB_CLEAR_SUPERBLOCK, "[clear_superblock] Created freelist node at address %lu with segment size %u\n", node_addr, segment_size);
        fl_node = make_freelist_node(node_addr);
        sb->free_count++;
        DEBUG(DB_CLEAR_SUPERBLOCK, "[clear_superblock] Adding freelist node to freelist\n");
        if (sb->freelist == NULL) sb->freelist = fl_node;
        if (prev) prev->next = fl_node;
        prev = fl_node;
    }

    DEBUG(DB_CLEAR_SUPERBLOCK, "[clear_superblock] free_count=%u\n", sb->free_count);
}

struct superblock* make_superblock(void * begin, int segment_size) {
    TOP = mem_sbrk(SB_SIZE);
    DEBUG(DB_MAKE_SUPERBLOCK, "[make_superblock] Incrementing TOP to %p\n", TOP);

    if (TOP == NULL || TOP = (void *) -1) {
        fprintf(stderr, "[make_superblock] mem_sbrk failed, ran out of memory\n");
        return NULL;
    }

    DEBUG(DB_MAKE_SUPERBLOCK, "[make_superblock] Creating superblock at address %lu with segment size %d\n", begin, segment_size);

    // keep all superblock information in first segment of superblock
    struct superblock* sb = (struct superblock *) begin;
    sb->seg_size = 0; // clear superblock needs this to check if this is a newly reclaimed guy
    sb->freelist = NULL;
    sb->next = NULL;
    sb->prev = NULL;
    sb->reclaimed = false;

    clear_superblock(sb, segment_size);
    return sb;
}

////////////////////////////////// MAIN WORKHORSE FUNCTIONS ///////////////////////
int mm_init(void) {
    // TODO for now only have superblocks which allocate in segment size MIN
    for (int i = 0; i < NUM_SEGS; i++)
        sb_map[i] = NULL;

    TOP = NULL;
    BOTTOM = NULL;

    return 0;
}


void * mm_malloc(size_t size) {
    DEBUG(DB_MALLOC, "[mm_malloc] Trying to allocate memory chunk of size %u\n", size);

    //TODO for now
    if (size > MAX_SEG_SIZE / 2) return NULL;

    DEBUG(DB_MALLOC, "[mm_malloc] looking through sb_map to find sb_list with correct seg size\n");
    struct superblock* sb_list;
    unsigned int seg_size, seg_size_idx;
    for (int i = 0; i < size; i++) {
        if (size <= SEG_SIZES[i]) {
            sb_list = sb_map[i];
            seg_size = SEG_SIZES[i];
            seg_size_idx = i;
            DEBUG(DB_MALLOC, "[mm_malloc] Found superblock linked list with seg size %d\n", seg_size);
            break;
        }
    }

    // now, we know which sb_list we want
    // go through each superblock and see if there is anything in the freelists
    struct superblock* sb;
    struct superblock* last_sb = NULL;
    struct freelist_node* fl_node;
    void * start_addr = NULL;

    DEBUG(DB_MALLOC, "[mm_malloc] Traversing through superblocks to find non-empty free list...\n");
    if (sb_list) {
        DEBUG(DB_MALLOC, "[mm_malloc] Superblock list is non-empty\n");
    } else {
        DEBUG(DB_MALLOC, "[mm_malloc] Superblock list is empty\n");
    }

    for (sb = sb_list; sb != NULL; sb = sb->next) {
        // last_sb is the last superblock in the linked list for this segsize
        last_sb = sb;

        DEBUG(DB_MALLOC, "[mm_malloc] Checking superblock at address %p and segment size %d\n", sb, sb->seg_size);
        fl_node = sb->freelist;
        if (fl_node) {
            DEBUG(DB_MALLOC, "[mm_malloc] Found non-empty freelist\n");
            // freelist is non-empty
            // move freelist pointer 1 over
            sb->freelist = sb->freelist->next;

            if (sb->free_count == 0) {
                fprintf(stderr, "[mm_malloc] Error: free_count < 0\n");
                return NULL;
            }
            sb->free_count--;

            // get start address of node
            start_addr = fl_node;

            memset(fl_node, EMPTY_MEM_CHAR, sb->seg_size);
            break;
        }
    }

    // found memory in freelist
    if (start_addr != NULL) return start_addr;
    DEBUG(DB_MALLOC, "[mm_malloc] No non-empty freelists found.\n");

    DEBUG(DB_MALLOC, "[mm_malloc] Looking for superblock in reclaimed superblocks...\n");

    struct superblock* new_sb;
    if (sb_freelist) {
        DEBUG(DB_MALLOC, "[mm_malloc] Found superblock in reclaimed superblocks\n");
        DEBUG(DB_MALLOC, "[mm_malloc] Removing superblock from reclaimed superblocks\n");
        new_sb = sb_freelist;
        sb_freelist = new_sb->next;

        DEBUG(DB_MALLOC, "[mm_malloc] Clearing superblock\n");
        clear_superblock(new_sb, seg_size);
        DEBUG(DB_MALLOC, "[mm_malloc] Got back cleared superblock starting at %p, seg_size %d, free_count %u\n", new_sb, new_sb->seg_size, new_sb->free_count);

        DEBUG(DB_MALLOC, "[mm_malloc] Clearing next and previous pointers for reclaimed superblock\n");
        new_sb->next = NULL;
        new_sb->prev = NULL;
    } else {
        DEBUG(DB_MALLOC, "[mm_malloc] No reclaimed superblocks. Creating a new one.\n");
        new_sb = make_superblock(TOP, seg_size);

        if (! new_sb) return NULL;
    }

    if (last_sb) {
        // add it to the back of sb_list
        DEBUG(DB_MALLOC, "[mm_malloc] Adding newly created superblock to back of sb_list\n");
        last_sb->next = new_sb;
        new_sb->prev = last_sb;
    } else {
        DEBUG(DB_MALLOC, "[mm_malloc] This is the first superblock with seg size %d\n", seg_size);
        if (TOP == NULL) {
            DEBUG(DB_MALLOC, "[mm_malloc] This is the very first superblock\n");
            TOP = dseg_hi;
            BOTTOM = dseg_lo;
        }
        sb_list = new_sb;
        sb_map[seg_size_idx] = sb_list;
    }

    sb = new_sb;

    DEBUG(DB_MALLOC, "[mm_malloc] Taking off node from new superblock's freelist...\n");

    // and use the first item in the newly-created superblock's freelist
    fl_node = sb->freelist;
    if (sb->free_count == 0) {
        fprintf(stderr, "[mm_malloc] Error: free_count < 0\n");
        return NULL;
    }

    sb->free_count--;
    DEBUG(DB_MALLOC, "[mm_malloc] New free_count = %u\n", sb->free_count);

    DEBUG(DB_MALLOC, "[mm_malloc] Updating freelist pointers\n");
    sb->freelist = sb->freelist->next;
    DEBUG(DB_MALLOC, "[mm_malloc] Got node with starting address %lu and segment size %d\n", fl_node, sb->seg_size);

    start_addr = fl_node;
    memset(fl_node, EMPTY_MEM_CHAR, sb->seg_size);

    DEBUG(DB_MALLOC, "[mm_malloc] Returning address %lu\n", start_addr);
    return start_addr;
}


void mm_free(void * addr) {
    DEBUG(DB_FREE, "[mm_free] Trying to free memory starting at address %p\n", addr);

    struct superblock * sb = get_sb(addr);
    DEBUG(DB_FREE, "[mm_free] Found correct superblock. It has starting address %p and segment size %d\n", sb, sb->seg_size);

    // create a new freelist node for this segment
    struct freelist_node* node = make_freelist_node(addr);
    DEBUG(DB_FREE, "[mm_free] Created new freelist node at address %p of size %d\n", addr, sb->seg_size);
    // add the freelist node to front of superblock's freelist
    node->next = sb->freelist;
    sb->free_count++;
    sb->freelist = node;

    int free_threshold = SB_SIZE / sb->seg_size - get_sb_header_seg_size(sb);

    if (sb->free_count == free_threshold) {
        DEBUG(DB_FREE, "[mm_free] Reclaiming superblock at address %p\n", sb);
        sb->reclaimed = true;

        DEBUG(DB_FREE, "[mm_free] Updating pointers of adjacent superblocks\n");
        if (sb->prev) sb->prev->next = sb->next;
        if (sb->next) sb->next->prev = sb->prev;

        if (sb->prev == NULL) {
            DEBUG(DB_FREE, "[mm_free] Updating pointer for sb_map\n");
            // get my seg size index
            unsigned int seg_size_idx;
            for (int i = 0; i < NUM_SEGS;i++) {
                if (SEG_SIZES[i] == sb->seg_size) {
                    seg_size_idx = i;
                    break;
                }
            }
            sb_map[seg_size_idx] = sb->next;
        }

        if (sb_freelist) {
            DEBUG(DB_FREE, "[mm_free] Adding reclaimed superblock to sb_freelist\n");
            sb->next = sb_freelist;
            sb_freelist->prev = sb;
            sb_freelist = sb;
        } else {
            DEBUG(DB_FREE, "[mm_free] This is the first reclaimed superblock\n");
            sb_freelist = sb;
        }
    } else {
        DEBUG(DB_FREE, "[mm_free] Not reclaiming this block\n");
        DEBUG(DB_FREE, "[mm_free] free_count=%u\n", sb->free_count);
        DEBUG(DB_FREE, "[mm_free] free_count to free=%d\n", free_threshold);
    }
}

// COLORS
#define RED "\x1B[31m"
#define GREEN "\x1B[32m"
#define BLUE "\x1B[34m"
#define YELLOW "\x1B[33m"
#define CYAN "\x1b[36m"
#define WHITE "\x1B[37m"

/////////////// DEBUGGING INFO FTW //////////////

/**
 * Print the current memory allocation
 * Colors:
 *  + red       ->  unallocated
 *  + yellow    ->  reclaimed superblock
 *  + blue      ->  free node inside superblock
 *  + green     ->  allocated
 *  + cyan      ->  superblock data
 */
void mm_printState(void) {
    struct freelist_node* node;
    struct superblock* sb;
    bool found;

    DEBUG(DB_PRINT, "[mm_printState] TOP currently set at %p\n", TOP);
    void * start_addr = BOTTOM;
    void * end_addr = TOP;
    DEBUG(DB_PRINT, "[mm_printState] Memory starts at address %lu and ends at address %lu\n", start_addr, end_addr);

    // allocated memory
    for (void * a = start_addr; a < end_addr; a += SB_SIZE) {
        sb = (struct superblock *) a;
        if (sb->reclaimed) {
            void * end_addr = ((void *) sb) + ((unsigned long) SB_SIZE);
            // paint all the memory locations as reclaimed
            for (char * addr = (char *) sb; addr < (char *) end_addr; addr++) {
                printf(YELLOW "%c", *addr);
            }
        } else {
            // the first segsize is superblock struct data
            for (int i = 0; i < sb->seg_size * get_sb_header_seg_size(sb); i++) {
                printf(CYAN "%c", '_');
            }

            void * start_addr = ((void *) sb) + sb->seg_size * get_sb_header_seg_size(sb);
            void * end_addr = ((void *) sb) + ((unsigned long) SB_SIZE);

            for (char * addr = start_addr; addr < (char *) end_addr; addr += sb->seg_size) {
                found = false;
                // hunt through the freelist nodes to see if this address is in there or not
                for (node = sb->freelist; node != NULL; node = node->next) {
                    if ((void *) node == addr) {
                        // found it, paint the entire node
                        for (char * it = addr; it < addr + sb->seg_size; it++) {
                            printf(BLUE "%c", *it);
                        }
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    // did not find any node in freelist corresponding to this address
                    // color the entire segment
                    for (char * it = addr; it < addr + sb->seg_size; it++) {
                        printf(GREEN "%c", *it);
                    }
                }
            }
        }
    }
    printf(WHITE "\n");
}
