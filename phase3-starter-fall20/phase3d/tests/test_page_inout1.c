/*
 * 1 child, 4 pages, 2 frames.
 * Create a disk with PAGES tracks. Child writes pages 0-1, reads the next pages 2-3.
 * Then read pages 0-1 and 2-3 again. 
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
 
#define PAGES		4
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
    char        buffer[128];
    char        *string;
    Sys_GetPID(&pid);
    Debug("Child starting %d\n",numBlocks);
    USLOSS_Console("Stats before touching the pages:\n");
    P3_PrintStats(&P3_vmStats);

    for (page = 0; page < 2; page++) {
        sprintf(buffer, "Child wrote page %d\n", page);
        string = (char *) (vmRegion + (page * USLOSS_MmuPageSize()));
        strcpy(string, buffer);
        USLOSS_Console(buffer);
    }
    USLOSS_Console("Stats after writing to the first two pages\n");
    P3_PrintStats(&P3_vmStats);

    for (page = 2; page < PAGES; page++) {
        c = *(char *) (vmRegion + (page * USLOSS_MmuPageSize()));
        sprintf(buffer, "Child read page %d\n", page);
        USLOSS_Console(buffer);
    }
    USLOSS_Console("Stats after reading the last two pages\n");
    P3_PrintStats(&P3_vmStats);

    
    for (page = 0; page < 2; page++) {
        sprintf(buffer, "Child wrote page %d\n", page);
        USLOSS_Console(buffer);
        string = (char *) (vmRegion + (page * USLOSS_MmuPageSize()));
        if (strcmp(string, buffer) != 0)
	   USLOSS_Console("Oops!  Read wrong buffer\n");
    }
    USLOSS_Console("Stats after reading first two pages\n");
    P3_PrintStats(&P3_vmStats);


    for (page = 2; page < PAGES; page++) {
        c = *(char *) (vmRegion + (page * USLOSS_MmuPageSize()));
        sprintf(buffer, "Child touched page %d\n", page);
        USLOSS_Console(buffer);
    }    
    USLOSS_Console("Stats after reading last two pages\n");

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
        TEST(P3_vmStats.faults, 8);
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
