/*
 * phase3b.c
 * Phase2 part B
 * group
 * Omar, Isabel
 * November 9, 2020
 */

#include <assert.h>
#include <phase1.h>
#include <phase2.h>
#include <usloss.h>
#include <string.h>
#include <libuser.h>

#include "phase3Int.h"

void
P3PageFaultHandler(int type, void *arg)
{
   
   int cause;
   int rc;
   int offset;
   cause = USLOSS_MmuGetCause();
   if (cause == USLOSS_MMU_FAULT) {
        USLOSS_PTE *checker;
        rc = P3PageTableGet(P1_GetPid(), &checker);
        USLOSS_Console("PAGE FAULT!!! PID %d page %d\n", P1_GetPid(), rc);

        //if the process does not have a page table  (P3PageTableGet)
        //  print error message
        //  USLOSS_Halt(1)
        if (checker == NULL) {
            USLOSS_Console("PAGE FAULT!!! PID %d has no page table!!!\n", P1_GetPid());
            USLOSS_Halt(1);
        } else {
            // determine which page suffered the fault
            offset = (int) arg / USLOSS_MmuPageSize();
            // update the page's PTE to map page x to frame x
            // set the PTE to be read-write and incore
            checker[offset].frame  = offset;
            checker[offset].read   = 1;
            checker[offset].write  = 1;
            checker[offset].incore = 1;
            // update the page table in the MMU
            rc = USLOSS_MmuSetPageTable(checker);
        }
    } else {
      if (cause == USLOSS_MMU_ACCESS){
        offset = (int) arg / USLOSS_MmuPageSize();
        USLOSS_Console("PROTECTION VIOLATION!!! PID %d offset 0x%x!!!\n", P1_GetPid(), offset);
      }
      else{
        USLOSS_Console("Unknown cause: %d\n", cause);
      }
        USLOSS_Halt(1);
    }
}

USLOSS_PTE *
P3PageTableAllocateEmpty(int pages)
{
    int i;
    // allocate and initialize an empty page table
    USLOSS_PTE *table = malloc(sizeof(USLOSS_PTE) * pages);
    if (table != NULL) {
        for (i = 0; i < pages; i++) {
            table[i].incore = 0;
            table[i].read   = 0;
            table[i].write  = 0;
            table[i].frame  = -1;
        }
    }
    return table;
}
