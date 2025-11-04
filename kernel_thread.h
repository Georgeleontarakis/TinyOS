#ifndef __KERNEL_THREAD_H
#define __KERNEL_THREAD_H

#include "tinyos.h"  			// For Tid_t and Task
#include "kernel_sched.h"		// For TCB
#include "kernel_proc.h"		// For PCB
#include "util.h"				// For rlnode and intrusive lists


struct TCB;


typedef struct PTCB {

	// --- Connection with Scheduler
	struct TCB* tcb;		// Kernel thread that scheduler runs


	// ---Body of process thread---//
	Task task;				// Function that thread executes
	int arg1;				// Number of arguments passed to the function
	void* args; 				// Pointer to the argument 


	// ---Condition for join/detach/exit---
	int exitval;			// Return value of ThreadJoin
	int exited;				// Has it stopped?
	int detached			// Is joinable or not?
	CondVar exit_cv;		// condvar: where joiners sleep
	int refcount;			// For safe cleanup (joiners/detach)


	// --- Intrusive list: Node inside PTCB list of PCB ---
	rlnode ptcb_list_node;
	

} PTCB;




#endif
