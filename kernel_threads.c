
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"

/** 
  @brief Create a new thread in the current process.
  */
Tid_t sys_CreateThread(Task task, int argl, void* args)
{
	
    // Έλεγχος ορθότητας
    if (task == NULL) return 0;

    PCB* pcb = CURPROC;        // η διεργασία που το καλεί
    if (pcb == NULL) return 0;

    // --- Δημιουργία και αρχικοποίηση του PTCB ---
    PTCB* ptcb = malloc(sizeof(PTCB));
    if (!ptcb) return 0;

    memset(ptcb, 0, sizeof(ptcb));
    ptcb->task     = task;
    ptcb->argl     = argl;
    ptcb->args     = args;
    ptcb->exitval  = 0;
    ptcb->exited   = 0;
    ptcb->detached = 0;
    ptcb->refcount = 1;  // 1 αναφορά για τον ίδιο του τον εαυτό

    cond_init(&ptcb->exit_cv);
    rlnode_init(&ptcb->ptcb_list_node, ptcb);

    // --- Δημιουργία TCB (kernel-level thread) ---
    TCB tcb = spawn_thread(pcb, start_process_thread);
    if (!tcb) {
        free(ptcb);
        return 0;
    }

    // --- Σύνδεση PTCB και TCB ---
    ptcb->tcb  = tcb;
    tcb->ptcb  = ptcb;

    // --- Προσθήκη στη λίστα threads του process ---
    rlist_push_back(&pcb->ptcb_list, &ptcb->ptcb_list_node);
    pcb->thread_count++;

    // --- Δημιουργία Tid_t ως pointer στο PTCB ---
    Tid_t tid = (Tid_t)ptcb;

    // --- Ξεκίνα το thread (προσθήκη στο ready queue) ---
    wakeup(tcb);

    return tid;
}

/**
  @brief Return the Tid of the current thread.
 */
Tid_t sys_ThreadSelf()
{
  return (Tid_t)(cur_thread()->ptcb);
	//return (Tid_t) cur_thread();
}

/**
  @brief Join the given thread.
  */
int sys_ThreadJoin(Tid_t tid, int* exitval)
{

    if (tid == 0) return -1;  // άκυρο thread id

    PTCB* target = (PTCB*)tid;  // μετατροπή του tid σε pointer προς PTCB
    if (!target) return -1;

    // Αν είναι το ίδιο thread με το τρέχον -> λάθος (δεν μπορείς να κάνεις join τον εαυτό σου)
    if (target == CURTHREAD->ptcb)
        return -1;

    // Αν είναι detached -> αποτυχία (δεν μπορείς να το κάνεις join)
    if (target->detached)
        return -1;

    // Αν δεν έχει τελειώσει ακόμα, περίμενε
    while (!target->exited)
        cond_wait(&target->exit_cv);

    // Αν υπάρχει pointer για exitval, αποθήκευσε την τιμή εξόδου
    if (exitval)
        *exitval = target->exitval;

    // Μείωσε το refcount — αν φτάσει στο 0, κάνε cleanup
    target->refcount--;
    if (target->refcount == 0)
        free(target); 

    return 0;

}

/**
  @brief Detach the given thread.
  */
int sys_ThreadDetach(Tid_t tid)
{

    if (tid == 0)
        return -1;



    PTCB* target = (PTCB*)tid;    // Μετατροπή του thread ID σε pointer
    if (!target)
        return -1;

    // Δεν μπορείς να κάνεις detach τον εαυτό σου (προαιρετικός έλεγχος)
    if (target == CURTHREAD->ptcb)
        return -1;

    // Αν είναι ήδη detached -> αποτυχία
    if (target->detached)
        return -1;

    // Μαρκάρουμε ότι είναι detached
    target->detached = 1;

    // Αν έχει ήδη τελειώσει (exited == 1), τότε κανείς δεν θα το περιμένει
    // οπότε μπορούμε να το καθαρίσουμε τώρα.
    if (target->exited) {
        target->refcount--;
        if (target->refcount == 0)
            free(target);
    }
    else {
        // Διαφορετικά, μειώνουμε το refcount κατά 1 γιατί δεν θα γίνει join
        target->refcount--;
    }

    return 0;
  
}

/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval)
{
// Πάρε το τρέχον PTCB
    PTCB* pt = CURTHREAD->ptcb;

    // Αν κάτι πάει στραβά (ποτέ δεν πρέπει)
    if (!pt)
        sched_finish_thread();

    // Αποθήκευση της τιμής εξόδου
    pt->exitval = exitval;
    pt->exited  = 1;

    // Αν είναι joinable (όχι detached) -> ξύπνα όσους περιμένουν
    if (!pt->detached) {
        cond_broadcast(&pt->exit_cv);
    } else {
        // Detached thread -> δεν το περιμένει κανείς, μπορεί να καθαριστεί άμεσα
        pt->refcount--;
        if (pt->refcount == 0)
            free(pt);
    }

    // Ενημέρωσε το PCB: ένα thread λιγότερο
    PCB* pcb = CURPROC;
    if (pcb) {
        pcb->thread_count--;
        // Αν δεν έχει απομείνει κανένα thread, η διεργασία τελειώνει
        if (pcb->thread_count == 0)
            sys_Exit(pt->exitval);
    }

    // Τερμάτισε το τρέχον kernel thread (TCB)
    sched_finish_thread();
  
}

