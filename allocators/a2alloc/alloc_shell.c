/**
 * Shell for the allocator
 * Written by Daniel Kats
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "allocator.c"

void print_commands() {
    printf("Valid commands:\n");
    printf("* malloc (size) [char]\n");
    printf("\tReturns the address of allocated memory and shows the memory\n");
    printf("\tOptionally fill the memory with given character\n");
    printf("* free [addr]\n");
    printf("\tFrees the memory at the given address. This must be an address returned by malloc\n");
    printf("* ?\n");
    printf("\tPrints the help\n");
    printf("* exit | quit | q\n");
    printf("\tQuit shell\n");
}

void print_error(char* command) {
    printf("Invalid command. Type ? to see the help. Type q to exit.\n");
}

void print_banner () {
    printf("Allocator shell version 0.0.1\n");
    printf("Written by Daniel Kats\n");
    printf("Page size is %lu. Superblock size is %lu.\n", PAGE_SIZE, SB_SIZE);
    printf("Starting address is %p\n", TOP);
    printf("Type \"?\" for help, type \"quit\" to exit\n");
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

    DEBUG(DB_PRINT, "[mm_printState] TOP currently set at %lu\n", TOP);
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
////////////////////////////////////// MAIN LOOP ////////////////////////////

int main() {
    const int MAX_COMMAND = 20;
    char buf[MAX_COMMAND];
    char defChar = 'a' - 1;

    mm_init();
    print_banner();
    mm_printState();

    while (1) {
        printf("> ");
        if (fgets(buf, MAX_COMMAND, stdin) == NULL) break;

        // kill trailing newline
        for (int i = 0; i < MAX_COMMAND; i++) {
            if (buf[i] == '\n') {
                buf[i] = '\0';
                break;
            }
        }
        /*printf("command: %s\n", buf);*/

        // length + 1 to capture null terminator
        if (strncmp(buf, "quit", 5) == 0 || strncmp(buf, "exit", 5) == 0 || strncmp(buf, "", 1) == 0 || strncmp(buf, "q", 2) == 0) {
            break;
        }

        if (strncmp(buf, "?", 2) == 0) {
            print_commands();
        } else if (strncmp(buf, "malloc ", 7) == 0) {
            // STARTSWITH

            size_t size = 0;
            int c, i;
            bool flag = false;
            for (i = 7; i < MAX_COMMAND; i++) {
                if (buf[i] == ' ' || buf[i] == '\0') break;

                c = buf[i] - '0';
                if (c < 0 || c > 9) {
                    printf("Error: first argument to malloc must be an int\n");
                    flag = true;
                }
                size *= 10;
                size += c;
            }

            if (flag) continue;
            if (size == 0) {
                printf("Error: cannot malloc size 0\n");
                continue;
            }

            char * addr = (char *) mm_malloc(size);
            if (addr == NULL) {
                printf("Failed to allocate memory!\n");
                continue;
            }

            char memChar;
            if (buf[i] != '\0') {
                // get the character to allocate
                // skip over the space
                memChar = buf[i + 1];
            } else {
                defChar++;
                if (defChar > 'z') defChar = 'a';
                memChar = defChar;
            }

            printf("Got back address %p, %lu for humans\n", addr, addr);
            printf("Using character '%c' to fill %u bytes of memory\n", memChar, size);
            for (char * it = addr; it < (addr + size); it++) {
                 *it = memChar;
            }

            mm_printState();
        } else if (strncmp(buf, "free ", 5) == 0) {
            // STARTSWITH

            unsigned long addr = 0;
            int c, i;
            bool flag = false;
            for (i = 5; i < MAX_COMMAND; i++) {
                if (buf[i] == ' ' || buf[i] == '\0') break;

                c = buf[i] - '0';
                if (c < 0 || c > 9) {
                    printf("Error: first argument to free must be an int\n");
                    flag = true;
                }
                addr *= 10;
                addr += c;
            }

            if (flag) continue;

            printf("Trying to free at addr %lu\n", addr);
            mm_free((void *) addr);

            mm_printState();
        } else {
            print_error(buf);
        }
    }

    return 0;
}
