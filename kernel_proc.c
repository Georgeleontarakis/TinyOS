
#include <assert.h>
#include "kernel_cc.h"
#include "kernel_proc.h"
#include "kernel_streams.h"


/* 
 The process table and related system calls:
 - Exec
 - Exit
 - WaitPid
 - GetPid
 - GetPPid

 */

/* The process table */
PCB PT[MAX_PROC];
unsigned int process_count;

PCB* get_pcb(Pid_t pid)
{
  return PT[pid].pstate==FREE ? NULL : &PT[pid];
}

Pid_t get_pid(PCB* pcb)
{
  return pcb==NULL ? NOPROC : pcb-PT;
}

/* Initialize a PCB */
static inline void initialize_PCB(PCB* pcb)
{
  pcb->pstate = FREE;
  pcb->argl = 0;
  pcb->args = NULL;

  for(int i=0;i<MAX_FILEID;i++)
    pcb->FIDT[i] = NULL;

  rlnode_init(& pcb->children_list, NULL);
  rlnode_init(& pcb->exited_list, NULL);
  rlnode_init(& pcb->children_node, pcb);
  rlnode_init(& pcb->exited_node, pcb);
<<<<<<< HEAD
  rlnode_init(& pcb->ptcb_list, pcb);     // Added
  pcb->thread_count = 0;                  // Added
=======

  rlnode_init(& pcb->ptcb_list, pcb); /*--initialize threads_list with pcb--*/
  pcb->thread_count = 0;               //added 
>>>>>>> be861c059cf012a0e6a7ba140b44aeda0ecfcaff
  pcb->child_exit = COND_INIT;
}


static PCB* pcb_freelist;

void initialize_processes()
{
  /* initialize the PCBs */
  for(Pid_t p=0; p<MAX_PROC; p++) {
    initialize_PCB(&PT[p]);
  }

  /* use the parent field to build a free list */
  PCB* pcbiter;
  pcb_freelist = NULL;
  for(pcbiter = PT+MAX_PROC; pcbiter!=PT; ) {
    --pcbiter;
    pcbiter->parent = pcb_freelist;
    pcb_freelist = pcbiter;
  }

  process_count = 0;

  /* Execute a null "idle" process */
  if(Exec(NULL,0,NULL)!=0)
    FATAL("The scheduler process does not have pid==0");
}


/*
  Must be called with kernel_mutex held
*/
PCB* acquire_PCB()
{
  PCB* pcb = NULL;

  if(pcb_freelist != NULL) {
    pcb = pcb_freelist;
    pcb->pstate = ALIVE;
    pcb_freelist = pcb_freelist->parent;
    process_count++;
  }

  return pcb;
}

/*
  Must be called with kernel_mutex held
*/
void release_PCB(PCB* pcb)
{
  pcb->pstate = FREE;
  pcb->parent = pcb_freelist;
  pcb_freelist = pcb;
  process_count--;
}


/*
 *
 * Process creation
 *
 */

/*
	This function is provided as an argument to spawn,
	to execute the main thread of a process.
*/
void start_main_thread()
{
  int exitval;

  Task call =  CURPROC->main_task;
  int argl = CURPROC->argl;
  void* args = CURPROC->args;

  exitval = call(argl,args);
  Exit(exitval);
}


/*
	System call to create a new process.
 */
Pid_t sys_Exec(Task call, int argl, void* args)
{
  PCB *curproc, *newproc;
  
  /* The new process PCB */
  newproc = acquire_PCB();

  if(newproc == NULL) goto finish;  /* We have run out of PIDs! */

  if(get_pid(newproc)<=1) {
    /* Processes with pid<=1 (the scheduler and the init process) 
       are parentless and are treated specially. */
    newproc->parent = NULL;
  }
  else
  {
    /* Inherit parent */
    curproc = CURPROC;

    /* Add new process to the parent's child list */
    newproc->parent = curproc;
    rlist_push_front(& curproc->children_list, & newproc->children_node);

    /* Inherit file streams from parent */
    for(int i=0; i<MAX_FILEID; i++) {
       newproc->FIDT[i] = curproc->FIDT[i];
       if(newproc->FIDT[i])
          FCB_incref(newproc->FIDT[i]);
    }
  }


  /* Set the main thread's function */
  newproc->main_task = call;

  /* Copy the arguments to new storage, owned by the new process */
  newproc->argl = argl;
  if(args!=NULL) {
    newproc->args = malloc(argl);
    memcpy(newproc->args, args, argl);
  }
  else
    newproc->args=NULL;

  /* 
    Create and wake up the thread for the main function. This must be the last thing
    we do, because once we wakeup the new thread it may run! so we need to have finished
    the initialization of the PCB.
   */
  if(call != NULL) {
    
      // --- Δημιουργία του PTCB για το main thread ---
      PTCB* ptcb = malloc(sizeof(PTCB));
      memset(ptcb, 0, sizeof(*ptcb));

      ptcb->task = call;       // η main συνάρτηση του προγράμματος
      ptcb->argl = argl;       // παράμετροι του main
      ptcb->args = args;
      ptcb->exitval = 0;
      ptcb->exited = 0;
      ptcb->detached = 0;
      ptcb->refcount = 1;
      cond_init(&ptcb->exit_cv);
      rlnode_init(&ptcb->ptcb_list_node, ptcb);

      // --- Δημιουργία του TCB για το main thread ---
      newproc->main_thread = spawn_thread(newproc, start_main_thread);

      // --- Σύνδεση PTCB και TCB ---
      ptcb->tcb = newproc->main_thread;
      newproc->main_thread->ptcb = ptcb;

      // --- Σύνδεση PTCB με PCB ---
      rlist_push_back(&newproc->ptcb_list, &ptcb->ptcb_list_node);
      newproc->thread_count = 1;

      // --- Ξεκίνημα του thread ---
      wakeup(newproc->main_thread);
     
  }


finish:
  return get_pid(newproc);
}


/* System call */
Pid_t sys_GetPid()
{
  return get_pid(CURPROC);
}


Pid_t sys_GetPPid()
{
  return get_pid(CURPROC->parent);
}


static void cleanup_zombie(PCB* pcb, int* status)
{
  if(status != NULL)
    *status = pcb->exitval;

  rlist_remove(& pcb->children_node);
  rlist_remove(& pcb->exited_node);

  release_PCB(pcb);
}


static Pid_t wait_for_specific_child(Pid_t cpid, int* status)
{

  /* Legality checks */
  if((cpid<0) || (cpid>=MAX_PROC)) {
    cpid = NOPROC;
    goto finish;
  }

  PCB* parent = CURPROC;
  PCB* child = get_pcb(cpid);
  if( child == NULL || child->parent != parent)
  {
    cpid = NOPROC;
    goto finish;
  }

  /* Ok, child is a legal child of mine. Wait for it to exit. */
  while(child->pstate == ALIVE)
    kernel_wait(& parent->child_exit, SCHED_USER);
  
  cleanup_zombie(child, status);
  
finish:
  return cpid;
}


static Pid_t wait_for_any_child(int* status)
{
  Pid_t cpid;

  PCB* parent = CURPROC;

  /* Make sure I have children! */
  int no_children, has_exited;
  while(1) {
    no_children = is_rlist_empty(& parent->children_list);
    if( no_children ) break;

    has_exited = ! is_rlist_empty(& parent->exited_list);
    if( has_exited ) break;

    kernel_wait(& parent->child_exit, SCHED_USER);    
  }

  if(no_children)
    return NOPROC;

  PCB* child = parent->exited_list.next->pcb;
  assert(child->pstate == ZOMBIE);
  cpid = get_pid(child);
  cleanup_zombie(child, status);

  return cpid;
}


Pid_t sys_WaitChild(Pid_t cpid, int* status)
{
  /* Wait for specific child. */
  if(cpid != NOPROC) {
    return wait_for_specific_child(cpid, status);
  }
  /* Wait for any child */
  else {
    return wait_for_any_child(status);
  }

}


void sys_Exit(int exitval)
{


    PCB* curproc = CURPROC;  // cache for efficiency

    // 1️⃣ Αποθήκευση της τιμής εξόδου
    curproc->exitval = exitval;

    // 2️⃣ Αν είμαι το init process (PID = 1), περίμενε να τελειώσουν όλα τα παιδιά
    if (get_pid(curproc) == 1) {

        while (sys_WaitChild(NOPROC, NULL) != NOPROC);

    } else {

        // 3️⃣ Reparent τυχόν ενεργά παιδιά στο init process
        PCB* initpcb = get_pcb(1);
        while (!is_rlist_empty(&curproc->children_list)) {
            rlnode* child = rlist_pop_front(&curproc->children_list);
            child->pcb->parent = initpcb;
            rlist_push_front(&initpcb->children_list, child);
        }

        // 4️⃣ Μεταφορά exited παιδιών στη λίστα του init και ειδοποίηση
        if (!is_rlist_empty(&curproc->exited_list)) {
            rlist_append(&initpcb->exited_list, &curproc->exited_list);
            kernel_broadcast(&initpcb->child_exit);
        }

        // 5️⃣ Δήλωσε στον parent ότι το process μου πέθανε
        rlist_push_front(&curproc->parent->exited_list, &curproc->exited_node);
        kernel_broadcast(&curproc->parent->child_exit);
    }

    #ifdef DEBUG
    assert(is_rlist_empty(&curproc->children_list));
    assert(is_rlist_empty(&curproc->exited_list));
    #endif

    // 6️⃣ Κλείσε όλα τα αρχεία του process
    for (int i = 0; i < MAX_FILEID; i++) {
        if (curproc->FIDT[i] != NULL) {
            FCB_decref(curproc->FIDT[i]);
            curproc->FIDT[i] = NULL;
        }
    }

    // 7️⃣ Ελευθέρωσε τα arguments του process (κρατάμε την if ✅)
    if (curproc->args) {
        free(curproc->args);
        curproc->args = NULL;
    }

    // 8️⃣ Καθάρισε όλα τα threads (PTCBs) που είχε το process
    while (!is_rlist_empty(&curproc->ptcb_list)) {
        rlnode* node = rlist_pop_front(&curproc->ptcb_list);
        PTCB* pt = node->ptcb;
        free(pt);
    }

    // 9️⃣ Αποσύνδεσε το main_thread
    curproc->main_thread = NULL;

    // 🔟 Δήλωσε ότι η διεργασία τερμάτισε
    curproc->pstate = ZOMBIE;

    // 1️⃣1️⃣ Κοιμήσου μέχρι να σε “μαζέψει” ο parent (WaitChild)
    kernel_sleep(EXITED, SCHED_USER);
}


Fid_t sys_OpenInfo()
{
	return NOFILE;
}

