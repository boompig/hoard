from a2_prelim import Hoard, debug, DEBUG, MEMORY, TOP_ADDR

if __name__ == "__main__":
    debug("Memory size is %d" % len(MEMORY))
    hoard = Hoard()
    hoard.print_state()

    #before we begin K_threshold = 1, F_threshold = 0.5

    #allocating 3 chunks of size 16 for thread 1
    loc = hoard.malloc(1, 16)    
    for i in range(16):
        MEMORY[i + loc] = '1'
    hoard.print_state()
    free_loc_1 = loc
    
    loc = hoard.malloc(1, 16)
    for i in range(16):
        MEMORY[i + loc] = '1'
    hoard.print_state()
    free_loc_2 = loc

    loc = hoard.malloc(1, 16)
    for i in range(16):
        MEMORY[i + loc] = '1'
    hoard.print_state()

    #allocating 1 chunk of size 32 for thread 1
    loc = hoard.malloc(1, 32)
    for i in range(32):
        MEMORY[i + loc] = '1'
    hoard.print_state()

    #size 32 block is freed from thread 1, and superblock returned to global heap because K > 1, and f < 0.5
    #still need to implement better sb selection algorithm, for now it just returns the one it operated on.
    hoard.free(1, loc)
    hoard.print_state()

    #second size 16 block is freed from thread 1, and superblock is not returned because K == 1
    hoard.free(1, free_loc_1)
    hoard.print_state()

    #allocating 1 chunk of size 32 for thread 2, this will get the superblock returned to heap 0, 
    #if it was any other size, it will get new page because empty page list is not implemented yet
    loc = hoard.malloc(2, 32)
    for i in range(32):
        MEMORY[i + loc] = '2'
    hoard.print_state()

    #allocating another 1 chunk of size 32 for thread 1
    loc = hoard.malloc(1, 32)
    for i in range(32):
        MEMORY[i + loc] = '1'
    hoard.print_state()

    #free another size 16 chunk from thread 1, this will cause superblock to be moved to heap 0 because K > 1, and f < 0.5
    #still need to implement better sb selection algorithm, for now it just returns the one it operated on.
    hoard.free(1, free_loc_2)
    hoard.print_state()

    #allocating 1 chunk of size 16 for thread 2, this will get the superblock returned to heap 0, 
    #if it was any other size, it will get new page because empty page list is not implemented yet
    #note that this is an instance of false sharing    
    loc = hoard.malloc(2, 16)
    for i in range(16):
        MEMORY[i + loc] = '2'
    hoard.print_state()    
