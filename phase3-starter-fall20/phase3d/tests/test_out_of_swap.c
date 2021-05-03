/*
 * 1 child, 21 pages, 1 frame.
 * Create a disk with 10 tracks.  Force page outs
 * until there is no more space on the disk. Check that the process terminates.
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
 
#define PAGES		21
#define CHILDREN 	1
#define FRAMES 		1
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
    char	*string;
    char	buffer[128];
    int		pid;

    int 	page;
 
    Sys_GetPID(&pid);
    Debug("Child starting %d\n",numBlocks);
    USLOSS_Console("Stats before touching the pages:\n");
    P3_PrintStats(&P3_vmStats);

    for (page = 0; page < numBlocks; page++) {
        sprintf(buffer, "Child wrote page %d\n", page);
        string = (char *) (vmRegion + (page * USLOSS_MmuPageSize()));
        strcpy(string, buffer);
    }
    sprintf(buffer, "Child wrote %d pages\n", numBlocks);
    USLOSS_Console("%s", buffer);
    P3_PrintStats(&P3_vmStats);
    USLOSS_Console("Let's touch one more page...\n");
    string = (char *) (vmRegion + (page * USLOSS_MmuPageSize()));
    strcpy(string, buffer); 
    
    USLOSS_Console("Shouldn't see this!\n");
    passed = FALSE;
    Sys_Terminate(1);
    return 0;
}

int P4_Startup(void *arg)
{
	int	child;
	int	pid;
	int 	tracks;
	int 	sectorsPerBlock;
	int 	blocksPerTrack;
        int     rc;
        rc = Sys_VmInit(PAGES, PAGES, FRAMES, PAGERS, (void **) &vmRegion);
        TEST(rc, P1_SUCCESS);

	USLOSS_Console("Pages: %d, Frames: %d, Children %d, Priority %d.\n",
		PAGES, FRAMES, CHILDREN, PRIORITY);

	tracks = 10;

	sectorsPerBlock = USLOSS_MmuPageSize() / USLOSS_DISK_SECTOR_SIZE;
	blocksPerTrack = USLOSS_DISK_TRACK_SIZE / sectorsPerBlock;
	numBlocks = (tracks ) * blocksPerTrack;
	printf("Page size:%d, Sectors/block:%d, Blocks/track:%d, numBlocks:%d\n", USLOSS_MmuPageSize(),sectorsPerBlock,blocksPerTrack,numBlocks);
	if (numBlocks < PAGES) {
		rc = Sys_Spawn("Child", Child, 0, USLOSS_MIN_STACK, PRIORITY, &pid);
		assert(rc == P1_SUCCESS);
                rc = Sys_Wait(&pid, &child);
                TEST(child, P3_OUT_OF_SWAP);
                PASSED();
	}
	else
		USLOSS_Console("Disk too big\n");
        USLOSS_Console("Stats after touching the pages:\n");
        Sys_VmShutdown();
        return 0; 
}


void test_setup(int argc, char **argv) {
    DeleteAllDisks();
    int rc = Disk_Create(NULL, P3_SWAP_DISK, 10);
    assert(rc == 0);
}

void test_cleanup(int argc, char **argv) {
    DeleteAllDisks();
    if (passed) {
        USLOSS_Console("TEST PASSED.\n");
    }
}
