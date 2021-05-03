/*
 * phase3c.c
 * Omar, Isabel
 * Group
 * Phase 2 Part C
 * November 30, 2020
 */

#include <assert.h>
#include <phase1.h>
#include <phase2.h>
#include <usloss.h>
#include <string.h>
#include <libuser.h>

#include "phase3.h"
#include "phase3Int.h"

#ifdef DEBUG
int debugging3 = 1;
#else
int debugging3 = 0;
#endif

void debug3(char *fmt, ...)
{
    va_list ap;

    if (debugging3) {
        va_start(ap, fmt);
        USLOSS_VConsole(fmt, ap);
    }
}



int *freeFrames;
int *usedMap;

typedef struct Fault {
    PID pid;  
    int offset;   
    int cause;  
    SID wait;      
    int kill;       
    int stat;           
} Fault;

struct Node {
    Fault *fault;          
    struct Node *next;   
};

typedef struct PagerInfo {
    SID sid;            
    PID pid;        
} PagerInfo;

static int init = FALSE;
static int initPager = FALSE;
static struct Node *faultQueue = NULL;
static int shutDown = FALSE;
static PagerInfo pagerTracker[P3_MAX_PAGERS];
static int totalPagers;
static SID pagerSem;


void enqueue(Fault *f);

void dequeue(Fault **retVal);

static int Pager(void *arg);


/*
 *----------------------------------------------------------------------
 *
 * P3FrameInit --
 *
 *  Initializes the frame data structures.
 *
 * Results:
 *   P3_ALREADY_INITIALIZED:    this function has already been called
 *   P1_SUCCESS:                success
 *
 *----------------------------------------------------------------------
 */
int
P3FrameInit(int pages, int frames)
{
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        int pid; Sys_GetPID(&pid); USLOSS_Console("Process %d called %s from user mode.\n", pid, __FUNCTION__); 
        USLOSS_IllegalInstruction(); 
        }
    
    if(init){
        return P3_ALREADY_INITIALIZED;
    }

    int result = P1_SUCCESS;
    // initialize the frame data structures, e.g. the pool of free frames
    // set P3_vmStats.freeFrames

    P3_vmStats.freeFrames = frames;
    freeFrames = malloc(sizeof(int) * frames);
    usedMap = malloc(sizeof(int) * frames);
    for (int i = 0; i < P3_vmStats.frames; i++){
        freeFrames[i] = TRUE;
        usedMap[i] = FALSE;
    }

    init = TRUE;
    return result;
}
/*
 *----------------------------------------------------------------------
 *
 * P3FrameShutdown --
 *
 *  Cleans up the frame data structures.
 *
 * Results:
 *   P3_NOT_INITIALIZED:    P3FrameInit has not been called
 *   P1_SUCCESS:            success
 *
 *----------------------------------------------------------------------
 */
int
P3FrameShutdown(void)
{
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        int pid; Sys_GetPID(&pid); USLOSS_Console("Process %d called %s from user mode.\n", pid, __FUNCTION__); 
        USLOSS_IllegalInstruction(); 
        }
    if(!init){
        return P3_NOT_INITIALIZED;
    }
    int result = P1_SUCCESS;
    free(freeFrames);
    free(usedMap);

    // clean things up

    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * P3FrameFreeAll --
 *
 *  Frees all frames used by a process
 *
 * Results:
 *   P3_NOT_INITIALIZED:    P3FrameInit has not been called
 *   P1_SUCCESS:            success
 *
 *----------------------------------------------------------------------
 */

int
P3FrameFreeAll(int pid)
{
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        int pid; Sys_GetPID(&pid); USLOSS_Console("Process %d called %s from user mode.\n", pid, __FUNCTION__); 
        USLOSS_IllegalInstruction(); 
        }
    if(!init){
        return P3_NOT_INITIALIZED;
    }
    if(pid < 0 || pid >= P1_MAXPROC){
        return P1_INVALID_PID;
    }
    int result = P1_SUCCESS;

    USLOSS_PTE *pageTable;
    int pt;
    pt = P3PageTableGet(pid, &pageTable);
    assert(pt == P1_SUCCESS);

    // free all frames in use by the process (P3PageTableGet)
    for (int i = 0; i < P3_vmStats.pages; i++){
        if(pageTable[i].incore){
            pageTable[i].incore = 0;
            freeFrames[pageTable[i].frame] = TRUE;
            P3_vmStats.freeFrames++;
        }
    }

    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * P3FrameMap --
 *
 *  Maps a frame to an unused page and returns a pointer to it.
 *
 * Results:
 *   P3_NOT_INITIALIZED:    P3FrameInit has not been called
 *   P3_OUT_OF_PAGES:       process has no free pages
 *   P1_INVALID_FRAME       the frame number is invalid
 *   P1_SUCCESS:            success
 *
 *----------------------------------------------------------------------
 */
int
P3FrameMap(int frame, void **ptr) 
{
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        int pid; Sys_GetPID(&pid); USLOSS_Console("Process %d called %s from user mode.\n", pid, __FUNCTION__); 
        USLOSS_IllegalInstruction(); 
        }
    if(!init){
        return P3_NOT_INITIALIZED;
    }

    if(frame<0 || frame >= P3_vmStats.frames){
        return P3_INVALID_FRAME;
    }

    int result = P1_SUCCESS;

    // get the page table for the process (P3PageTableGet)
    USLOSS_PTE *pageTable;
    int rc;
    rc = P3PageTableGet(P1_GetPid(), &pageTable);
    assert(rc == P1_SUCCESS);
    // find an unused page

    int op = -1;
    for (int i = 0; i < P3_vmStats.pages; i++) {
        if (!pageTable[i].incore) {
            op = i;
            continue;
        }
    }

    if(op==-1){
        return P3_OUT_OF_PAGES;
    }
    int pages;
    char *addr = USLOSS_MmuRegion(&pages);
    addr += op * USLOSS_MmuPageSize();
    *ptr = addr;
    // update the page's PTE to map the page to the frame
    pageTable[op].frame = frame;
    pageTable[op].incore = 1;
    pageTable[op].read = 1;
    pageTable[op].write = 1;
    usedMap[frame] = TRUE;

    // update the page table in the MMU (USLOSS_MmuSetPageTable)
    rc = USLOSS_MmuSetPageTable(pageTable);
    assert(rc == P1_SUCCESS);

    return result;
}
/*
 *----------------------------------------------------------------------
 *
 * P3FrameUnmap --
 *
 *  Opposite of P3FrameMap. The frame is unmapped.
 *
 * Results:
 *   P3_NOT_INITIALIZED:    P3FrameInit has not been called
 *   P3_FRAME_NOT_MAPPED:   process didn’t map frame via P3FrameMap
 *   P1_INVALID_FRAME       the frame number is invalid
 *   P1_SUCCESS:            success
 *
 *----------------------------------------------------------------------
 */
int
P3FrameUnmap(int frame) 
{
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        int pid; Sys_GetPID(&pid); USLOSS_Console("Process %d called %s from user mode.\n", pid, __FUNCTION__); 
        USLOSS_IllegalInstruction(); 
        }

    if(frame <0 || frame >= P3_vmStats.frames){
        return P3_INVALID_FRAME;
    }
    int result = P1_SUCCESS;

    USLOSS_PTE *pageTable;

    // get the process's page table (P3PageTableGet)
    int pt = P3PageTableGet(P1_GetPid(), &pageTable);
    assert(pt == P1_SUCCESS);
    // verify that the process mapped the frame
    int map = -1;
    for (int i = 0; i < P3_vmStats.pages; i++) {
        if (pageTable[i].incore && pageTable[i].frame == frame) {
            map = i;
        }
    }
    if(map == -1 || !usedMap[frame]){
        return P3_FRAME_NOT_MAPPED;
    }
    // update page's PTE to remove the mapping
    pageTable[map].incore = 0;
    usedMap[frame] = FALSE;
    // update the page table in the MMU (USLOSS_MmuSetPageTable)
    pt = USLOSS_MmuSetPageTable(pageTable);
    assert(pt == P1_SUCCESS);
    return result;
}



/*
 *----------------------------------------------------------------------
 *
 * FaultHandler --
 *
 *  Page fault interrupt handler
 *
 *----------------------------------------------------------------------
 */

static void
FaultHandler(int type, void *arg)
{
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        int pid; Sys_GetPID(&pid); USLOSS_Console("Process %d called %s from user mode.\n", pid, __FUNCTION__); 
        USLOSS_IllegalInstruction(); 
        }
    Fault   fault;
    char semName[P1_MAXNAME+1];

    fault.offset = (int) arg;
    // fill in other fields in fault
    fault.pid = P1_GetPid();
    fault.cause = USLOSS_MmuGetCause();
    fault.kill = FALSE;
    fault.stat = 0;

    snprintf(semName, sizeof(semName), "%d", fault.pid);
    int rc;
    rc = P1_SemCreate(semName, 0, &fault.wait);
    assert(rc == P1_SUCCESS);

    // add to queue of pending faults
    enqueue(&fault);
    // let pagers know there is a pending fault
    rc = P1_V(pagerSem);
    assert(rc == P1_SUCCESS);
    // wait for fault to be handled
    rc = P1_P(fault.wait);
    assert(rc == P1_SUCCESS);
    // kill off faulting process so skeleton code doesn't hang
    rc = P1_SemFree(fault.wait);
    assert(rc == P1_SUCCESS);
    if(fault.kill){
        P1_Quit(fault.stat);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * P3PagerInit --
 *
 *  Initializes the pagers.
 *
 * Results:
 *   P3_ALREADY_INITIALIZED: this function has already been called
 *   P3_INVALID_NUM_PAGERS:  the number of pagers is invalid
 *   P1_SUCCESS:             success
 *
 *----------------------------------------------------------------------
 */
int
P3PagerInit(int pages, int frames, int pagers)
{
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        int pid; Sys_GetPID(&pid); USLOSS_Console("Process %d called %s from user mode.\n", pid, __FUNCTION__); 
        USLOSS_IllegalInstruction(); 
        }
    if(pagers < 1 || pagers > P3_MAX_PAGERS){
        return P3_INVALID_NUM_PAGERS;
    }
    int     result = P1_SUCCESS;

    USLOSS_IntVec[USLOSS_MMU_INT] = FaultHandler;

    // initialize the pager data structures
    totalPagers = pagers;
    char semName[P1_MAXNAME + 1];
    int rc;
    for (int i = 0; i < pagers; i++){
        
        snprintf(semName, sizeof(semName), "%d", i);
        rc = P1_SemCreate(semName, 0, &pagerTracker[i].sid);
        assert(rc == P1_SUCCESS);
    }

    rc = P1_SemCreate("Sem", 0, &pagerSem);
    assert(rc == P1_SUCCESS);

    // fork off the pagers and wait for them to start running
    char pagerName[P1_MAXNAME + 1];
    for (int i = 0; i<pagers;i++){
        snprintf(pagerName, sizeof(pagerName), "%d", i);
        rc = P1_Fork(pagerName, Pager, (void *) i, USLOSS_MIN_STACK, P3_PAGER_PRIORITY, 0, &pagerTracker[i].pid);
        assert(rc == P1_SUCCESS);
        rc = P1_P(pagerTracker[i].sid);
        assert(rc == P1_SUCCESS);
    }
    initPager = TRUE;
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * P3PagerShutdown --
 *
 *  Kills the pagers and cleans up.
 *
 * Results:
 *   P3_NOT_INITIALIZED:     P3PagerInit has not been called
 *   P1_SUCCESS:             success
 *
 *----------------------------------------------------------------------
 */
int
P3PagerShutdown(void)
{
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        int pid; Sys_GetPID(&pid); USLOSS_Console("Process %d called %s from user mode.\n", pid, __FUNCTION__); 
        USLOSS_IllegalInstruction(); 
        }
    if(!initPager){
        return P3_NOT_INITIALIZED;
    }

    int result = P1_SUCCESS;
    int rc;
    shutDown = TRUE;
    // cause the pagers to quit
    for (int i = 0; i<totalPagers; i++){
        rc = P1_V(pagerSem);
        assert(rc == P1_SUCCESS);
    }
    // clean up the pager data structures
    for (int i = 0; i<totalPagers;i++){
        rc = P1_SemFree(pagerTracker[i].sid);
        pagerTracker[i].pid = -1;
        assert(rc == P1_SUCCESS);
    }
    rc = P1_SemFree(pagerSem);
    assert(rc == P1_SUCCESS);

    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * Pager --
 *
 *  Handles page faults
 *
 *----------------------------------------------------------------------
 */

static int
Pager(void *arg)
{
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        int pid; Sys_GetPID(&pid); USLOSS_Console("Process %d called %s from user mode.\n", pid, __FUNCTION__); 
        USLOSS_IllegalInstruction(); 
        }
    Fault *fault;
    int pageInx;
    void *page;
    int frame;
    USLOSS_PTE *pageTable;
    int pagerid;
    pagerid = (int) arg;

    int rc;
    rc = P1_V(pagerTracker[pagerid].sid);
    assert(rc == P1_SUCCESS);

    while(!shutDown){
        rc = P1_P(pagerSem);
        assert(rc == P1_SUCCESS);
        dequeue(&fault);

        if(fault !=NULL){
            if((*fault).cause == USLOSS_MMU_ACCESS){
                (*fault).kill = TRUE;
                (*fault).stat = USLOSS_MMU_ACCESS;
                rc = P1_V((*fault).wait);
                assert(rc == P1_SUCCESS);
                //break;
                continue;
            }
        frame = -1;
        for (int i = 0; i < P3_vmStats.frames && frame == -1; i++) {
            if (freeFrames[i]) {
                frame = i;
                }
            }
        if(frame==-1){
            rc = P3SwapOut(&frame);
            P3_vmStats.freeFrames++;
        }
        pageInx = (*fault).offset / USLOSS_MmuPageSize();
        rc = P3SwapIn((*fault).pid, pageInx, frame);

        if (rc == P3_EMPTY_PAGE){
            rc = P3FrameMap(frame, &page);
            assert(rc == P1_SUCCESS);
            memset(page, 0, USLOSS_MmuPageSize());
            rc = P3FrameUnmap(frame);
            assert(rc == P1_SUCCESS);

            P3_vmStats.new++;
        }
        else if(rc== P3_OUT_OF_SWAP){
            (*fault).kill = TRUE;
            (*fault).stat = P3_OUT_OF_SWAP;
            rc = P1_V((*fault).wait);
            assert(rc==P1_SUCCESS);
            //break;
            continue;
        }
        rc = P3PageTableGet((*fault).pid, &pageTable);
        assert(rc == P1_SUCCESS);
        pageTable[pageInx].frame = frame;
        pageTable[pageInx].incore = 1;
        pageTable[pageInx].read = 1;
        pageTable[pageInx].write = 1;
        P3_vmStats.freeFrames--;
        freeFrames[frame]=FALSE;
        rc = USLOSS_MmuSetPageTable(pageTable);
        assert(rc== P1_SUCCESS);
        rc = P1_V((*fault).wait);
        assert(rc == P1_SUCCESS);
        }
    }

    return 0;
}
void enqueue(Fault *fq) {
    struct Node* curr = faultQueue;
    if (curr == NULL) {
        faultQueue = malloc(sizeof(struct Node));
        faultQueue->fault = fq;
        faultQueue->next = NULL;
        return;
    }
    while (curr->next != NULL) {
        curr = curr->next;
    }
    curr->next = malloc(sizeof(struct Node));
    curr->next->fault = fq;
    curr->next->next = NULL;
}
void dequeue(Fault **rv) {
    if (faultQueue == NULL) {
        *rv = NULL;
    } else {
        struct Node *clean = faultQueue;
        *rv = faultQueue->fault;
        faultQueue = faultQueue->next;
        free(clean);
    }
}
