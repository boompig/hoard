from a2_prelim import Allocator, debug, DEBUG, MEMORY, TOP_ADDR

if __name__ == "__main__":
    debug("Memory size is %d" % len(MEMORY))
    alloc = Allocator()
    alloc.print_state()

    loc = alloc.malloc(100)

    debug("Got back address %d" % loc)
    debug("TOP now at %d" % TOP_ADDR)

    # write 100 characters
    for i in range(100):
        MEMORY[i + loc] = 'c'

    alloc.print_state()

    debug("Trying to free 100 obj...")
    alloc.free(loc)

    alloc.print_state()

    debug("Filling the freed 100...")
    loc = alloc.malloc(100)
    for i in range(100):
        MEMORY[i + loc] = 'd'

    addr = []

    for i in range(8):
        loc = alloc.malloc(16)
        debug("Got back address %d" % loc)
        for j in range(16):
            MEMORY[j + loc] = str(i)

        addr.append(loc)
    alloc.print_state()

    loc = alloc.malloc(16)
    debug("Got back address %d" % loc)
    for j in range(16):
        MEMORY[j + loc] = '!'

    alloc.print_state()
    alloc.free(addr[0])

    alloc.print_state()
