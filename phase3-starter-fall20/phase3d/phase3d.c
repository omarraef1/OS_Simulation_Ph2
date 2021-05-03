/*
 * phase3d.c
 * Omar, Isabel
 * Group
 * Phase2 part d
 * December 9, 2020
 */

/***************

NOTES ON SYNCHRONIZATION

There are various shared resources that require proper synchronization. 

Swap space. Free swap space is a shared resource, we don't want multiple pagers choosing the
same free space to hold a page. You'll need a mutex around the free swap space.

The clock hand is also a shared resource.

The frames are a shared resource in that we don't want multiple pagers to choose the same frame via
the clock algorithm. That's the purpose of marking a frame as "busy" in the pseudo-code below. 
Pagers ignore busy frames when running the clock algorithm.

A process's page table is a shared resource with the pager. The process changes its page table
when it quits, and a pager changes the page table when it selects one of the process's pages
in the clock algorithm. 

Normally the pagers would perform I/O concurrently, which means they would release the mutex
while performing disk I/O. I made it simpler by having the pagers hold the mutex while they perform
disk I/O.

***************/


#include <assert.h>
#include <phase1.h>
#include <phase2.h>
#include <usloss.h>
#include <string.h>
#include <libuser.h>

#include "phase3.h"
#include "phase3Int.h"

#ifdef DEBUG
static int debugging3 = 1;
#else
static int debugging3 = 0;
#endif

static void debug3(char *fmt, ...)
{
    	va_list ap;

    	if (debugging3) {
        	va_start(ap, fmt);
        	USLOSS_VConsole(fmt, ap);
    	}
}

static int init = FALSE;
struct Hold{
	int pid;
	int page;
	int frame;
	int track;
	int start;
	int total;
	int room;
	struct Hold * next;
};

struct InFrame{
	int pid;
	int frame;
	int page;
	struct InFrame *next;
};

struct Mutex {
	int pid;
	int sid;
	struct Mutex *next;
};

static int *chooseF ; // indicates whether frame is busy
static struct Hold *swapSpace;  // holds informaton about pages on disk
static struct Mutex* exclusive;  // hold the semaphores for the processes
static int size;		// holds the size of the page
static struct InFrame *frames;  // holds the information for pages in frames

/*
 *Creates the list (size of frames) to hold information about pages on a given frame. 
*/
void makeFrameList(int given){
	struct InFrame *prev = NULL;
	int going = 0;
	while (going < given){

		struct InFrame *temp = malloc(sizeof(struct InFrame));
		temp -> pid =-1;
		temp -> frame = going;
		temp -> page = -1;
		temp -> next = NULL;
		if (frames == NULL){
			frames = temp;
		}
		if (prev !=NULL){
			prev -> next = temp;
		}
		going +=1;
		prev = temp;
	}

}

/*
 *Creates the list to hold information about the swapspace.
*/
void makeHoldList(int space, int sectors, int total){
	int going= 0;
	int tracks = 0;
	int startSec = 0;
	struct Hold *prev = NULL;
	while (going < space){
		struct Hold *temp = malloc(sizeof(struct Hold));
		temp -> pid = -1;
		temp -> frame = -1;
		temp -> page = -1;
		temp -> room = 0; // can be filled
		temp -> next = NULL;
		temp -> start = startSec;
		temp -> total = sectors;
		temp -> track = tracks;
		startSec += sectors;
		if (startSec % total == 0){
			tracks += 1;
			startSec = 0;
		}
		if (swapSpace == NULL){
			swapSpace = temp;
		}
		if (prev != NULL){
			prev -> next = temp;
		}
		prev = temp;
		
		going +=1;
	}
}

/*
 *Checks that the function is called in kernel mode.
*/
static void check(){
	if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) != USLOSS_PSR_CURRENT_MODE){
		USLOSS_Console("Error: called from user mode.\n");
		USLOSS_IllegalInstruction();
	}
}

/*
 *----------------------------------------------------------------------
 *
 * P3SwapInit --
 *
 *  Initializes the swap data structures.
 *
 * Results:
 *   P3_ALREADY_INITIALIZED:    this function has already been called
 *   P1_SUCCESS:                success
 *
 *----------------------------------------------------------------------
 */
int
P3SwapInit(int pages, int frames)
{
   	int result = P1_SUCCESS;
	check();
	if (init ){
		return P3_ALREADY_INITIALIZED;
	}
	int num;
	int secsInTrack;
	int sectorSize;
	size= USLOSS_MmuPageSize();
	assert(P1_SUCCESS == P2_DiskSize(P3_SWAP_DISK, &sectorSize, &secsInTrack, &num));
	int sectorInPage = size/sectorSize;
	int space = (secsInTrack/sectorInPage) * num;
	makeHoldList(space, sectorInPage, secsInTrack);

	chooseF = malloc(sizeof(int)*frames);
	makeFrameList(frames);
	int i;
	for (i = 0; i < frames; i++){
		chooseF[i] = 0;
	}
	init = TRUE;
	exclusive = NULL;
	P3_vmStats.blocks = space;
	P3_vmStats.freeBlocks = space;
	P3_vmStats.freeFrames = frames;
	

    	return result;
}

/*
 *Creates the name and creates the semaphore for the process
 *Returns the semaphore id.
*/
int create(int pid){
	int sid;
	char name[P1_MAXNAME+1];
	snprintf(name, sizeof(name), "%s%d", "mutex", pid);
	assert(P1_SUCCESS == P1_SemCreate(name, 1, &sid));
	return sid;
}

/*
 *Goes through the linked list to find the semaphore for the process
 * if there is no node with a semaphore, one is created and added 
 * to the linked list, returns the semaphore id.
*/
int getSem(int pid){
	struct Mutex *temp = NULL;
	int sid = 0;
	struct Mutex *prev = NULL;
	if (exclusive == NULL){
		sid = create( pid);
		exclusive = malloc(sizeof(struct Mutex));
		temp = exclusive;
		temp ->sid =sid;
		temp -> pid = pid;
		temp ->next = NULL;
	}else{// search for
		temp = exclusive;
		while (temp != NULL){
			if (temp ->pid == pid){
				return temp->sid;
			}
			prev = temp;
			temp = temp ->next;
		}

	}
	if (temp == NULL){
		sid = create(pid);
		prev -> next = malloc(sizeof(struct Mutex));
		temp = prev->next;
		temp ->next = NULL;
		temp ->sid = sid;
		temp ->pid = pid;
	}
	return temp -> sid;
}

/*
 *----------------------------------------------------------------------
 *
 * P3SwapShutdown --
 *
 *  Cleans up the swap data structures.
 *
 * Results:
 *   P3_NOT_INITIALIZED:    P3SwapInit has not been called
 *   P1_SUCCESS:            success
 *
 *----------------------------------------------------------------------
 */
int
P3SwapShutdown(void)
{
	int result = P1_SUCCESS;

	check();
	if (!init){
		return P3_NOT_INITIALIZED;
	}

	free(chooseF);

	struct InFrame* temp3;
	while (temp3!=NULL){
		temp3 = frames -> next;
		free(frames);
		frames = temp3;
	}
	
	struct Hold* temp2;
	while (swapSpace != NULL){
		temp2 = swapSpace ->next;
		free(swapSpace);
		swapSpace = temp2;
	}

	struct Mutex *temp;
	while(exclusive !=NULL){
		temp = exclusive ->next;
		free(exclusive);
		exclusive = temp;
	}

    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * P3SwapFreeAll --
 *
 *  Frees all swap space used by a process
 *
 * Results:
 *   P3_NOT_INITIALIZED:    P3SwapInit has not been called
 *   P1_SUCCESS:            success
 *
 *----------------------------------------------------------------------
 */

int
P3SwapFreeAll(int pid)
{
    	int result = P1_SUCCESS;
	check();
	if (!init){
		return P3_NOT_INITIALIZED;
	}
    /*****************

    P(mutex)
    free all swap space used by the process
    V(mutex)

    *****************/
	int mut = getSem(pid);
	assert(P1_SUCCESS == P1_P(mut));
	struct Hold* temp = swapSpace;
	while(temp != NULL){
		if (temp -> pid == pid){
			temp -> pid = -1;
			temp ->frame = -1;
			temp -> page = -1;
			temp -> room = 0;
			//P3_vmStats.freeFrames++ ;
			P3_vmStats.freeBlocks++;//
		}
		temp = temp ->next;
	}
	assert(P1_SUCCESS == P1_V(mut));

    	return result;
}

/*
 *Searches for the node representing the frame being looked for.
 * Returns pointer to the node.
*/
struct InFrame *getFrame(int frame){
	struct InFrame *temp = frames;
	while (temp !=NULL){
		if (temp -> frame == frame){
			return temp;
		}
		temp = temp -> next;
	}
	return NULL;

}


/*
 *----------------------------------------------------------------------
 *
 * P3SwapOut --
 *
 * Uses the clock algorithm to select a frame to replace, writing the page that is in the frame out 
 * to swap if it is dirty. The page table of the page’s process is modified so that the page no 
 * longer maps to the frame. The frame that was selected is returned in *frame. 
 *
 * Results:
 *   P3_NOT_INITIALIZED:    P3SwapInit has not been called
 *   P1_SUCCESS:            success
 *
 *----------------------------------------------------------------------
 */
int
P3SwapOut(int *frame) 
{
	check();
    	int result = P1_SUCCESS;
	if (!init){
		return P3_NOT_INITIALIZED;
	}


	static int hand = -1;
	int pid = P1_GetPid();
	int mut = getSem(pid);
	int target;

	assert(P1_SUCCESS== P1_P(mut));	

	int frames = P3_vmStats.frames;
	int access = 0;
	while(TRUE){
		hand = (hand+1)%frames;
		if (chooseF[hand] == 0){ // if not busy
			assert(P1_SUCCESS == USLOSS_MmuGetAccess(hand, &access));
			if ( (access &USLOSS_MMU_REF) == 0){// if not referenced
				target = hand;
				break;
			}else{
				// clear reference bit, USLOSS_MmuSetAccess
				assert(P1_SUCCESS ==  USLOSS_MmuSetAccess(hand,access&USLOSS_MMU_DIRTY)); 
			}
		}
	}

	if ((access& USLOSS_MMU_DIRTY) == 2){
		// write page to its location on the swap disk
		struct Hold *temp = swapSpace;
		struct InFrame *temp2 = getFrame(target);
		while (temp != NULL){
			if (temp -> pid == temp2->pid && temp -> page == temp2->page){
				break;
			}
			temp = temp -> next;
		}
		void *address;
		int rc = P3FrameMap(target, &address);
		void *buffer = malloc(size);
		memcpy(buffer, address, size);
		assert(P1_SUCCESS == P2_DiskWrite(P3_SWAP_DISK, temp->track, temp->start, temp->total,buffer));
		free(buffer);
		rc = P3FrameUnmap(target);
		
		// clear dirt bit USLOSS_MmuSetAccess
		assert(P1_SUCCESS == USLOSS_MmuSetAccess(target, access&USLOSS_MMU_REF));
	}
	
	USLOSS_PTE *pte;
	struct Hold *temp = swapSpace;
	while (temp != NULL){
		if (temp -> frame == target){
			int found = temp -> pid;
			assert(P1_SUCCESS ==P3PageTableGet(found, &pte));
			int i;
			for (i = 0;i< P3_vmStats.pages; i++){
				if (pte[i].frame == target){
					pte[i].incore =FALSE;
					pte[i].frame = -1;
				}
			}
		}
		temp = temp->next;
	}

	assert(P1_SUCCESS == USLOSS_MmuSetPageTable(pte));


	chooseF[target] = 1; // frame is busy	
	P3_vmStats.pageOuts++;
	assert(P1_SUCCESS == P1_V(mut)); 	
	*frame = target;


    	return result;
}

/*
 *Looks for the swapSpace being used by the given process and page.
 * If not there then returns Null, else returns pointer to the node.
*/
struct Hold * getSpace(int pid, int page){
	struct Hold *temp = swapSpace;
	while (temp != NULL){
		if ((temp -> room == 1) && (temp -> pid == pid) && (temp->page == page)){
			return temp; // found it
		}
		temp = temp -> next;
	}

	return temp;
}

/*
 *----------------------------------------------------------------------
 *
 * P3SwapIn --
 *
 *  Opposite of P3FrameMap. The frame is unmapped.
 *
 * Results:
 *   P3_NOT_INITIALIZED:     P3SwapInit has not been called
 *   P1_INVALID_PID:         pid is invalid      
 *   P1_INVALID_PAGE:        page is invalid         
 *   P1_INVALID_FRAME:       frame is invalid
 *   P3_EMPTY_PAGE:          page is not in swap
 *   P1_OUT_OF_SWAP:         there is no more swap space
 *   P1_SUCCESS:             success
 *
 *----------------------------------------------------------------------
 */
int
P3SwapIn(int pid, int page, int frame)
{
	check();
    	int result = P1_SUCCESS;

	if (!init){
		return P3_NOT_INITIALIZED;
	}
	if ((pid<0) || (pid>= P1_MAXPROC)){
		return P1_INVALID_PID;
	}
	if((page <0) || (page >= P3_vmStats.pages)){
		return P3_INVALID_PAGE;
	}
	if ((frame < 0 ) || (frame >= P3_vmStats.frames)){
		return P3_INVALID_FRAME;
	}


	void *address;
	int mut = getSem(pid);
	assert (P1_SUCCESS == P1_P(mut));		
	struct InFrame *temp = getFrame(frame);
	temp -> pid = pid;
	temp ->page = page;

	struct Hold *space = getSpace(pid, page);
	if (space!= NULL){  // if on disk reading into frame
		space ->frame = frame;
		assert(P1_SUCCESS== P3FrameMap(frame, &address));
		char * buffer = malloc(size);
		assert (P1_SUCCESS == P2_DiskRead(P3_SWAP_DISK, space->track, space->start, space->total, buffer));
		memcpy(address, buffer, size);
		free(buffer);
		assert(P1_SUCCESS == P3FrameUnmap(frame));
		P3_vmStats.pageIns++;
	}else{
		// allocate space for page on swap disk
		struct Hold *temp = swapSpace;
		while (temp !=NULL){
			if (temp -> room ==0){
				temp -> pid = pid;
				temp -> page = page;
				temp -> frame = frame;
				temp -> room = 1; // occupied
				break;
			}
			temp = temp -> next;
		}

		if (temp == NULL){
		// out of space
			result = P3_OUT_OF_SWAP;
		}else{
			result = P3_EMPTY_PAGE;
		}
		P3_vmStats.freeBlocks--;
	}
	chooseF[frame] = 0;// not busy

	assert(P1_SUCCESS == P1_V(mut));

    	return result;
}
