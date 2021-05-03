/*
 * 1 child, 20 pages, 10 frames.
 * Create a disk with 20 tracks. Child touches first ten pages, 
 * then touches the next ten pages. 10 pageOuts and 20 page faults.
 * Check deterministic statistics.
 */

#include <usyscall.h>
#include <libuser.h>
#include <assert.h>
#include <string.h>
#include <usloss.h>
#include <stdlib.h>
#include <phase3.h>
#include <libdisk.h>

#include "tester.h"
#include "phase3Int.h"
 
#define PAGES		20
#define CHILDREN 	1
#define FRAMES 		(PAGES / 2)
#define PRIORITY	1
#define PAGERS          1
#define ENTRIES 	PAGES


#define assume(expr) {							\
    if (!(expr)) {							\
	printf("%s:%d: failed assumption `%s'\n", __FILE__, __LINE__, 	\
	    #expr);							\
	abort();							\
    }									\
}


#ifdef DEBUG
static int debugging = 1;
#else
static int debugging = 0;
#endif /* DEBUG */

static void
Debug(char *fmt, ...)
{   
    va_list ap;
    
    if (debugging) { 
        va_start(ap, fmt);
        USLOSS_VConsole(fmt, ap);
    }
}


int numBlocks;
void *vmRegion;
static int passed = FALSE;

int
Child(void *arg)
{
    int		pid;
    int 	page;
    char        c;
    Sys_GetPID(&pid);
    Debug("Child starting %d\n",numBlocks);
    USLOSS_Console("Stats before touching the pages:\n");
    P3_PrintStats(&P3_vmStats);

    for (page = 0; page < PAGES / 2; page++) {
      c = *(char *)(vmRegion + (page * (int)USLOSS_MmuPageSize()));
    }
    USLOSS_Console("Stats after touching 10 pages\n");
    P3_PrintStats(&P3_vmStats);

    for (page = PAGES / 2; page < PAGES; page++) {
        c = *(char *) (vmRegion + (page * (int)USLOSS_MmuPageSize()));
    }
    USLOSS_Console("Stats after touching last 10 pages\n"); 
    Sys_Terminate(1);
    return 0;
}

int P4_Startup(void *arg)
{
	int	child;
	int	pid;
        int     rc;
        rc = Sys_VmInit(PAGES, PAGES, FRAMES, PAGERS, (void **) &vmRegion);
        TEST(rc, P1_SUCCESS);

	USLOSS_Console("Pages: %d, Frames: %d, Children %d, Priority %d.\n",
		PAGES, FRAMES, CHILDREN, PRIORITY);

        rc = Sys_Spawn("Child", Child, 0, USLOSS_MIN_STACK, PRIORITY, &pid);
        assert(rc == P1_SUCCESS);
        rc = Sys_Wait(&pid, &child);
        TEST(child, 1);
        TEST(P3_vmStats.faults, 20);
        TEST(P3_vmStats.pageOuts, 10);
        PASSED();
        Sys_VmShutdown();
        return 0; 
}


void test_setup(int argc, char **argv) {
    DeleteAllDisks();
    int rc = Disk_Create(NULL, P3_SWAP_DISK, PAGES);
    assert(rc == 0);
}

void test_cleanup(int argc, char **argv) {
    DeleteAllDisks();
    if (passed) {
        USLOSS_Console("TEST PASSED.\n");
    }
}
