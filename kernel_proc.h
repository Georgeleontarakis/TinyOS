#ifndef __KERNEL_PROC_H
#define __KERNEL_PROC_H

/**
  @file kernel_proc.h
  @brief The process table and process management.

  @defgroup proc Processes
  @ingroup kernel
  @brief The process table and process management.

  This file defines the PCB structure and basic helpers for
  process access.

  @{
*/ 

#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_thread.h"

#define PIPE_BUFFER_SIZE 20



/**
  @brief PID state

  A PID can be either free (no process is using it), ALIVE (some running process is
  using it), or ZOMBIE (a zombie process is using it).
  */
typedef enum pid_state_e {
  FREE,   /**< @brief The PID is free and available */
  ALIVE,  /**< @brief The PID is given to a process */
  ZOMBIE  /**< @brief The PID is held by a zombie */
} pid_state;

/**
  @brief Process Control Block.

  This structure holds all information pertaining to a process.
 */
typedef struct process_control_block {
  pid_state  pstate;      /**< @brief The pid state for this PCB */

  PCB* parent;            /**< @brief Parent's pcb. */
  int exitval;            /**< @brief The exit value of the process */

  TCB* main_thread;       /**< @brief The main thread */
  Task main_task;         /**< @brief The main thread's function */
  int argl;               /**< @brief The main thread's argument length */
  void* args;             /**< @brief The main thread's argument string */

  rlnode children_list;   /**< @brief List of children */
  rlnode exited_list;     /**< @brief List of exited children */

  rlnode children_node;   /**< @brief Intrusive node for @c children_list */
  rlnode exited_node;     /**< @brief Intrusive node for @c exited_list */

  CondVar child_exit;     /**< @brief Condition variable for @c WaitChild. 
                             This condition variable is  broadcast each time a child
                             process terminates. It is used in the implementation of
                             @c WaitChild() */

  FCB* FIDT[MAX_FILEID];  /**< @brief The fileid table of the process */

  rlnode ptcb_list;    /*--Added: list of all threads(PTCB's) of this task--*/
  int thread_count;    //added

} PCB;



typedef struct process_thread_control_block {

	// --- Connection with Scheduler
	TCB* tcb;		// Kernel thread that scheduler runs


	// ---Body of process thread---//
	Task task;				// Function that thread executes
	int argl;				// Number of arguments passed to the function
	void* args; 				// Pointer to the argument 


	// ---Condition for join/detach/exit---
	int exitval;			// Return value of ThreadJoin
	int exited;				// Has it stopped?
	int detached;			// Is joinable or not?
	CondVar exit_cv;		// condvar: where joiners sleep
	int refcount;			// For safe cleanup (joiners/detach)


	// --- Intrusive list: Node inside PTCB list of PCB ---
	rlnode ptcb_list_node;
	

} PTCB;


typedef struct pipe_control_block{
		FCB *reader, *writer;

		/* Conditional Variables*/
		CondVar has_space;				/* Used for blocking writer if no space is available */
		CondVar has_data;				/* Used for blocking reader if there is no data to be read*/


		int w_position, r_position;		/* To do */	
		char BUFFER[PIPE_BUFFER_SIZE];  /* Bounded cyclic byte buffer */

} pipe_cb;



/*
	Method to start a thread (not the main one)

*/
void start_thread();


/*
	Method used to initialize a ptcb
*/
PTCB* initialize_ptcb();

/**
  @brief Initialize the process table.

  This function is called during kernel initialization, to initialize
  any data structures related to process creation.
*/
void initialize_processes();

/**
  @brief Get the PCB for a PID.

  This function will return a pointer to the PCB of 
  the process with a given PID. If the PID does not
  correspond to a process, the function returns @c NULL.

  @param pid the pid of the process 
  @returns A pointer to the PCB of the process, or NULL.
*/
PCB* get_pcb(Pid_t pid);

/**
  @brief Get the PID of a PCB.

  This function will return the PID of the process 
  whose PCB is pointed at by @c pcb. If the pcb does not
  correspond to a process, the function returns @c NOPROC.

  @param pcb the pcb of the process 
  @returns the PID of the process, or NOPROC.
*/
Pid_t get_pid(PCB* pcb);

/** @} */

#endif
