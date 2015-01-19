from a2_prelim import Allocator, debug, DEBUG, MEMORY, TOP_ADDR

def print_commands():
    print "Valid commands:"
    print "* malloc (size) [char]"
    print "\tReturns the address of allocated memory and shows the memory"
    print "\tOptionally fill the memory with given character"
    print "* free [addr]"
    print "\tFrees the memory at the given address. This must be an address returned by malloc"
    print "* ?"
    print "\tPrints the help"
    print "* exit | quit | q"
    print "\tQuit shell"

def print_error(command):
    print "Invalid command. Type ? to see the help. Type q to exit."

def print_info(allocator):
    print "Allocator shell version 0.0.1"
    print "Memory has size %d" % len(MEMORY)
    print "Written by Daniel Kats"

if __name__ == "__main__":
    alloc = Allocator()
    print_info(alloc)

    # allocator shell
    while True:
        user_in = raw_input("> ")
        if user_in == "" or user_in == "exit" or user_in == "quit" or user_in == "q":
            break

        if user_in == "?":
            print_commands()
        elif user_in.startswith("malloc"):
            size = int(user_in.split(" ")[1])
            addr = alloc.malloc(size)

            if (len(user_in.split()) == 3):
                c = user_in.split(" ")[2][0]
                for i in range(size):
                    MEMORY[addr + i] = c

            print addr
            alloc.print_state()
        elif user_in.startswith("free"):
            addr = int(user_in.split(" ")[1])
            alloc.free(addr)
            alloc.print_state()
        else:
            print_error(user_in)
