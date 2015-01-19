CC			=	gcc
INCLUDES 	= 	$(TOPDIR)/include
LIBDIR 		= 	$(TOPDIR)/util
CC_FLAGS 	= 	-std=gnu99 -Wall -I$(INCLUDES) -L $(LIBDIR)
LIBS 		= 	-lmmutil -lpthread -lm
DEPENDS 	= 	$(LIBDIR)/libmmutil.a $(INCLUDES)/tsc.h $(INCLUDES)/mm_thread.h $(INCLUDES)/timer.h
MIN_CC_FLAGS=	-Wall -std=gnu99
DEBUG_MIN_CC_FLAGS=	-g -Wall -std=gnu99

all: shell

debug: shell-debug

test: tester.c
	$(CC) $(MIN_CC_FLAGS) tester.c -o test

clean:
	cd ..; make clean
	cd ../../util; make clean
	rm -f shell
	rm -f ./*.o

shell: alloc_shell.c allocator.c
	$(CC) $(MIN_CC_FLAGS) -o shell alloc_shell.c

shell-debug: alloc_shell.c allocator.c
	$(CC) $(DEBUG_MIN_CC_FLAGS) -o shell alloc_shell.c

.PHONY: all clean
#cd ..; make
#cd ../../util; make
#$(CC) $(CC_FLAGS) $^ -o $@ $(TOPDIR)/allocators/alloclibs/liba2alloc.a $(LIBS)
