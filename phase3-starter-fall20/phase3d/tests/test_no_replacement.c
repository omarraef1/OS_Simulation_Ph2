//No page replacement.
//pages=4, children=4, frames=16, priority=3, iterations=2

/*
 * test.c
 *	
 *	Tester for phase3. You can define the number of pages, frames,
 *	children, children priority, and iterations through loop.
 *	The children execute one-at-a-time, as determined by the
 *	controller process. Each child alternately writes a string to 
 *	each page of the VM region, then reads the pages back to make
 *	sure they have the correct contents. You'll probably want
 *	to start out with some simpler settings, such as 1 page, 1 frame,
 *	and 1 child.
 *
 */
#include <usyscall.h>
#include <libuser.h>
#include <assert.h>
#include <usloss.h>
#include <stdlib.h>
#include <phase3.h>
#include <stdarg.h>
#include <libdisk.h>
#include "tester.h"
#include "phase3Int.h"

#define PAGES		4
#define CHILDREN 	4
#define FRAMES 		(PAGES * CHILDREN)
#define PRIORITY	3
#define ITERATIONS	2
#define PAGERS          1

// Information about each child.
#define ALIVE 	1
#define DONE 	2

typedef struct Child {
    int		id;		/* Child's id */
    int		child;		/* Child waits for parent */
    int		parent;		/* Parent waits for child */
    int		status;		/* Child's status (see above) */
} Child;

Child	*children[CHILDREN];
Child	childInfo[CHILDREN];
int	alive = 0;
int	running;
int	currentChild;
void	*vmRegion;
int	pageSize;
static  int passed = FALSE;
char	*fmt = "** Child %d, page %d";


#define Rand(limit) ((int) (((double)((limit)+1)) * rand() / \
			((double) RAND_MAX)))
#ifdef DEBUG
int debugging = 1;
#else
int debugging = 0;
#endif /* DEBUG */


static void
debug(char *fmt, ...)
{
    va_list ap;

    if (debugging) {
	va_start(ap, fmt);
	USLOSS_Console(fmt, ap);
    }
}

int 
ChildProc(void *arg)
{
    Child	*childPtr = (Child *) arg;
    int		page;
    int		i;
    char	*target;
    char	buffer[128];
    int		pid;
    int		result;

    Sys_GetPID(&pid);
    childPtr->status = ALIVE;
    alive++;
    /*Tconsole("Child %d starting\n", childPtr->id);*/
    result = Sys_SemV(running);
    assert(result == 0);
    for (i = 0; i < ITERATIONS; i++) {
	debug("Child %d waiting\n", childPtr->id);
	result = Sys_SemP(childPtr->child);
	assert(result == 0);
	/*Tconsole("Child %d writing\n", childPtr->id);*/
	for (page = 0; page < PAGES; page++) {
	    sprintf(buffer, fmt, childPtr->id, page);
	    target = (char *) (vmRegion + (page * pageSize));
	    debug("Child %d writing page %d (0x%p): \"%s\"\n", 
		childPtr->id, page, target, buffer);
	    strcpy(target, buffer);
	}
	debug("Child %d resuming parent\n", childPtr->id);
	result = Sys_SemV(childPtr->parent);
	assert(result == 0);
	debug("Child %d waiting (2)\n", childPtr->id);
	result = Sys_SemP(childPtr->child);
	assert(result == 0);
	/*Tconsole("Child %d reading\n", childPtr->id);*/
	for (page = 0; page < PAGES; page++) {
	    sprintf(buffer, fmt, childPtr->id, page);
	    target = (char *) (vmRegion + (page * pageSize));
	    debug("Child %d reading page %d (0x%p)\n", childPtr->id, page,
		target);
	    if (strcmp(target, buffer)) {
		int frame;
		int protection;
		USLOSS_Console(
		    "Child %d: read \"%s\" from page %d (0x%p), not \"%s\"\n",
		    pid, target, page, target, buffer);
		USLOSS_Console("MMU contents:\n");
		for (i = 0; i < PAGES; i++) {
		    result = USLOSS_MmuGetMap(0, i, &frame, &protection);
		    if (result == USLOSS_MMU_OK) {
			USLOSS_Console("page %d frame %d protection 0x%x\n",
			    i, frame, protection);
		    }
		}
		abort();
	    }
	}
	if (i == (ITERATIONS - 1)) {
	    childPtr->status = DONE;
	}
	debug("Child %d resuming parent\n", childPtr->id);
	result = Sys_SemV(childPtr->parent);
	assert(result == 0);
    }
    Sys_Terminate(1);
    return 0;
}

int
Controller(void *arg)
{
    static int	index = 0;
    int		result;
    int		pid;

    Sys_GetPID(&pid);
    while(alive > 0) {
	/*
	 * Choose a child to do something
	 */
	index = (index + 1) % alive;
	debug("Pinging child %d (%d)\n", 
	    children[index]->id, children[index]->child);
	result = Sys_SemV(children[index]->child);
	assert(result == 0);
	debug("Waiting for child %d (%d)\n", 
	    children[index]->id, children[index]->child);
	result = Sys_SemP(children[index]->parent);
	assert(result == 0);
	if (children[index]->status == DONE) {
	    /*
	     * When a child is done replace it with the alive child 
	     * with the highest index, so that the alive children 
	     * are always in positions 0..alive-1 in the array.
	     */
	    alive--;
	    children[index] = children[alive];
	}
    }
    Sys_Terminate(2);
    return 0;
}


int
P4_Startup(void *arg)
{
    int		child;
    int		pid;
    int		i;
    int		result;
    char        buffer[128];
    char        buffer1[128];
    result = Sys_VmInit(PAGES, PAGES, FRAMES, PAGERS, (void **) &vmRegion);
    TEST(result, P1_SUCCESS);
    USLOSS_Console("Pages: %d, Frames: %d, Children %d, Iterations %d, Priority %d.\n",
	PAGES, FRAMES, CHILDREN, ITERATIONS, PRIORITY);
    result = Sys_SemCreate("running", 0, &running);
    assert(result == 0);
    
    
    pageSize = USLOSS_MmuPageSize();
    for (i = 0; i < CHILDREN; i++) {
	children[i] = &childInfo[i];
	children[i]->id = i;
        sprintf(buffer, "Child%d", i);
	result = Sys_SemCreate(buffer, 0, &children[i]->child);
	debug("Created child sem %d for child %d\n", 
	    children[i]->child, i);
	assert(result == 0);
        sprintf(buffer1, "Parent%d", i);
	result = Sys_SemCreate(buffer1, 0, &children[i]->parent);
	debug("Created parent sem %d for child %d\n", 
	    children[i]->parent, i);
	assert(result == 0);
	result = Sys_Spawn(buffer, ChildProc, (void *) children[i], USLOSS_MIN_STACK * 2, PRIORITY, &pid);
	assert(result == 0);
        result = Sys_SemP(running);
	assert(result == 0);
    }
    result = Sys_Spawn("Controller", Controller, NULL, USLOSS_MIN_STACK * 2, 1, &pid);
    assert(result == 0);
 
    for (i = 0; i < CHILDREN; i++) {
	result = Sys_Wait(&pid, &child);
        assert(result == 0);
    }
    result = Sys_Wait(&pid, &child);
    assert(result == 0);
    TEST(P3_vmStats.pageIns, 0);
    TEST(P3_vmStats.pageOuts, 0);
    TEST(P3_vmStats.faults, 16);
    PASSED();
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

