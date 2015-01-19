"""
A single-thread implementation of Hoard, just for testing purposes
"""

import sys

class bcolors:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'


LOG_B = 4
MAX_EXP = 8
PAGE_SIZE = 64
TOP_ADDR = 0
#MAX_MEMORY = 2 ** 14
MAX_MEMORY = 1024
MEMORY = ['#'] * MAX_MEMORY

# this should be in Alloc, but whatevs
sb_map = {}
DEBUG = False

def debug(s):
    if DEBUG:
        print "[DEBUG] " + s

class FreelistNode(object):
    """has start and size"""

    def __init__(self, start, size):
        self.start = start
        self.size = size

class Superblock(object):
    MULTIPLE = 2

    def __init__(self, seg_size, start_addr):
        self.seg_size = seg_size
        self.free_list = []
        self.start_addr = start_addr
        self.end_addr = self.start_addr + Superblock.size()
        addr = self.end_addr - seg_size

        while addr >= start_addr:
            #debug("Creating free node at address %d of size %d" % (addr, seg_size))
            self.free_list.append( FreelistNode(addr, addr + seg_size) )
            addr -= seg_size

    def free(self, addr):
        """Free memory from inside this superblock at addr.
        addr guaranteed to belong to this suberblock."""

        self.free_list.append( FreelistNode (addr, self.seg_size) )

    @staticmethod
    def size():
        return PAGE_SIZE * Superblock.MULTIPLE

class Seg(object):
    """Struct to hold superblocks.
    All superblocks are segmented in same size
    """

    def __init__(self, size):
        self.seg = size
        self.superblocks = []

    def malloc(self, size):
        debug("Looking through superblock freelists...")
        for sb in self.superblocks:
            if sb.free_list != []:
                # pop off a free list node
                node = sb.free_list.pop()
                debug("Found freed memory!")
                return node.start

        debug("Not found. Have to allocate new superblock of size 2^%d" % self.seg)
        return -1

    def push_sb(self, sb):
        self.superblocks.append(sb)
        node = sb.free_list.pop()
        debug("Grabbed a free node from superblock, at address %d" % node.start)
        return node.start
        

class Allocator(object):
    F_total = 0
    F_used = 0
    F_ratio = 0.5
    K_count = 0
    K_threshold = 1

    def __init__(self):
        self.segs = {}
        for i in range(LOG_B, MAX_EXP + 1):
            self.segs[i] = Seg(2 ** i)

    def find_seg_i(self, num):
        seg_i = -1
        for i in range(LOG_B, MAX_EXP + 1):
            if num <= 2 ** i:
                seg_i = i
                break
        return seg_i

    def free(self, addr):
        """This is hard :( """

        # step 1 -> what superblock
        # round to starting address of a superblock
        block_addr = (addr / Superblock.size()) * Superblock.size()
        sb = sb_map[block_addr]
        sb.free(addr)
        self.F_used = self.F_used - 1        
    
        seg_i = self.find_seg_i(sb.seg_size)
        debug("in free: seg_i is: %d" % seg_i)        
        #check for return to heap 0 condition here
        if (self.F_used / self.F_total < self.F_ratio and self.K_count > self.K_threshold):
            self.segs[seg_i].superblocks.remove(sb)
            self.K_count = self.K_count - 1
            self.F_total = self.F_total - Superblock.size() / sb.seg_size 
            return True 
        debug("in alloc free: K_count: %d, F_total: %d, F_used: %d" % (self.K_count, self.F_total, self.F_used))
        return False       

    def malloc(self, size):
        seg_i = self.find_seg_i(size)

        if seg_i == -1:
            return -1

        debug("Trying to malloc %d" % size)
        debug("TOP at %d" % TOP_ADDR)
        debug("Using seg size 2^%d = %d" % (seg_i, 2 ** seg_i))
        self.F_used = self.F_used + 1
        return self.segs[seg_i].malloc(size)

    def push_sb(self, seg_i, superblock):
        self.K_count = self.K_count + 1
        self.F_total = self.F_total + Superblock.size() / superblock.seg_size 
        self.F_used = self.F_used + 1
        debug("in push: K_count: %d, F_total: %d, F_used: %d" % (self.K_count, self.F_total, self.F_used))
        return self.segs[seg_i].push_sb(superblock)

    def is_freed(self, addr):
        sb_addr = (addr / Superblock.size()) * Superblock.size()
        for block in sb_map[sb_addr].free_list:
            if block.start <= addr and addr < block.start + block.size:
                return True
        return False

    def print_state(self):
        """Allocated and not freed = green
        Allocated and freed = yellow
        Not allocated = red
        """

        sys.stdout.write("\n")
        for i, c in enumerate(MEMORY):
            if i < TOP_ADDR:
                if self.is_freed(i):
                    sys.stdout.write(bcolors.WARNING + c + bcolors.ENDC)
                else:
                    sys.stdout.write(bcolors.OKGREEN + c + bcolors.ENDC)
            else:
                sys.stdout.write(bcolors.FAIL + c + bcolors.ENDC)

        sys.stdout.write("\n")

class Hoard(object):
    # plus one more for the global heap
    N_THREADS = 2 
    
    def __init__(self):
        self.alloc = [Allocator() for i in range(self.N_THREADS+1)]

    def calculate_seg_index(self, target_size):
        seg_index = -1
        for i in range(LOG_B, MAX_EXP + 1):
            if target_size <= 2 ** i:
                seg_index = i
                break
        return seg_index

    def init_superblock(self, seg_index):
        global TOP_ADDR
        # need a new superblock
        seg_size = 2** seg_index
        sb = Superblock(seg_size, TOP_ADDR)
        debug("Created superblock starting at addr %d" % TOP_ADDR)
        sb_map[TOP_ADDR] = sb
        TOP_ADDR += sb.size()
        return sb

    def free(self, thread_num, addr):
        is_return = self.alloc[thread_num].free(addr)
        #check if return to heap 0 condition is true
        if (is_return):
            addr_boarder = (addr / Superblock.size()) * Superblock.size()
            sb = sb_map[addr_boarder]
            seg_i = self.calculate_seg_index(sb.seg_size)
            self.alloc[0].segs[seg_i].superblocks.append(sb)
        

    def malloc(self, thread_num, size):
        res = self.alloc[thread_num].malloc(size)
        seg_i = self.calculate_seg_index(size)
        if(res == -1):
            if (self.alloc[0].segs[seg_i].superblocks):
                sb = self.alloc[0].segs[seg_i].superblocks.pop();
            else:
                sb = self.init_superblock(seg_i)
            
            res = self.alloc[thread_num].push_sb(seg_i, sb)
        return res

    def print_state(self):
        self.alloc[0].print_state();

