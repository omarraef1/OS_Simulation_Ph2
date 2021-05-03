/*
 * test_clean.c
 * Page replacement.  1 child, 2 pages, 1 frame. Disk with 2 tracks.
 * Child reads page 1, then page 2. Child writes
 * page 1 then page 2. Child reads back page 1.  
 * Check deterministic statistics. Should have 5 faults.
 *
 */

#include <usyscall.h>
#include <libuser.h>
#include <assert.h>
#include <usloss.h>
#include <stdlib.h>
#include <phase3.h>
#include <stdarg.h>
#include <unistd.h>
#include <libdisk.h>

#include "tester.h"
#include "phase3Int.h"

#define PAGES 2         // # of pages per process (be sure to try different values)
#define FRAMES (PAGES / 2)
#define ITERATIONS 1
#define PRIORITY 1
#define PAGERS 1        // # of pagers

static char *vmRegion;
static int  pageSize;

static int passed = FALSE;

#ifdef DEBUG
int debugging = 1;
#else
int debugging = 0;
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


static int
Child(void *arg)
{
    volatile char *name = (char *) arg;
    char    *page;
    char    *page2;
    char    message1[] = "Something for page1";  
    char    message2[] = "Something for page2";
    int     pid;
    
 
    Sys_GetPID(&pid);
    Debug("Child \"%s\" (%d) starting.\n", name, pid);
    P3_PrintStats(&P3_vmStats);
    // The first time a page is read it should be full of zeros.
    for (int j = 0; j < PAGES; j++) {
        page = vmRegion + j * pageSize;
        Debug("Child \"%s\" reading zeros from page %d @ %p\n", name, j, page);
        for (int k = 0; k < pageSize; k++) {
            TEST(page[k], '\0');
        }
    }
    USLOSS_Console("Stats after first read of the two pages\n");    
    P3_PrintStats(&P3_vmStats);
    
    page = (char *)(vmRegion);
    page2 = (char *)(vmRegion + pageSize);

    strcpy(page, message1);
    strcpy(page2, message2);

    USLOSS_Console("Stats after writing to the two pages\n");
    P3_PrintStats(&P3_vmStats);
 
    USLOSS_Console("Stats after reading the first page again\n");
    if(strcmp(page, message1) != 0){
       USLOSS_Console("TEST FAILED\n");
    }
    Debug("Child \"%s\" done.\n", name);
    return 0;
}


int
P4_Startup(void *arg)
{
    int     rc;
    int     pid;
    int     status;

    Debug("P4_Startup starting.\n");
    rc = Sys_VmInit(PAGES, PAGES, FRAMES, PAGERS, (void **) &vmRegion);
    TEST(rc, P1_SUCCESS);
    USLOSS_Console("PAGES: %d, FRAMES: %d, # of tracks: %d\n", PAGES, FRAMES, PAGES);
   
    pageSize = USLOSS_MmuPageSize();
    rc = Sys_Spawn("Child", Child, 0, USLOSS_MIN_STACK * 4, 3, &pid);
    assert(rc == P1_SUCCESS);
    rc = Sys_Wait(&pid, &status);
    assert(rc == P1_SUCCESS);
    TEST(status, 0);
    Debug("Child terminated\n");
    TEST(P3_vmStats.faults, 5);
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

