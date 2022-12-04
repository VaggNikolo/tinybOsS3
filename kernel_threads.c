
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"
#include "kernel_cc.h"
#include "kernel_streams.h"



void thread_initialization(){
  int exitval;

  Task call = CURTHREAD->ptcb->task;
  int argl = CURTHREAD->ptcb->argl;
  void* args = CURTHREAD->ptcb->args;

  exitval = call(argl,args);
  sys_ThreadExit(exitval);
}

/** 
  @brief Create a new thread in the current process.
  */
Tid_t sys_CreateThread(Task task, int argl, void* args)
{
  PTCB* ptcb = (PTCB*)xmalloc(sizeof(PTCB));     //Allocating space for a PTCB
  ptcb -> task = task;                           //Initializing the values once again
  ptcb -> argl = argl;
  ptcb->args=args;


  ptcb -> exitval= 0;
  ptcb -> exited = 0;   
  ptcb -> detached = 0;
  ptcb -> refcount = 0 ;
  ptcb -> exit_cv = COND_INIT;


  rlnode_init(&ptcb-> ptcb_list_node,ptcb); //Initializing our PTCB list


  if(ptcb-> task!= NULL) {
    ptcb->tcb= spawn_thread(CURPROC, thread_initialization); //Creating the TCB linked to our PTCB
    ptcb->tcb->ptcb= ptcb;  //Linking our PTCB and TCB
    rlist_push_back(&CURPROC->ptcb_list, &ptcb->ptcb_list_node); //Placing out PTCB in the last position of our PTCB list
    CURPROC->thread_count++;
    wakeup(ptcb->tcb);
  }

  return (Tid_t) ptcb;

}

/**
  @brief Return the Tid of the current thread.
 */
Tid_t sys_ThreadSelf()
{
	return (Tid_t) CURTHREAD->ptcb; //Returning the id of our PTCB
}

/**
  @brief Join the given thread.
  */
int sys_ThreadJoin(Tid_t tid, int* exitval)
{
	PTCB* ptcb = (PTCB*)tid;

//Multiple checks to decide if we can join the given thread
//-We need the thread to exist
//-We check if it actually exists in our list
//-A thread can't join itself
//-We check whether the thread is detached.
  
  if(tid == NOTHREAD || (rlist_find(&CURPROC->ptcb_list,ptcb,NULL)) == NULL || tid == sys_ThreadSelf()||ptcb->detached == 1){ //search the ptcb_list for the given tid (see if it exists)
      return -1; //If any of the above is true, of course the join attempt fails
  }
  
  ptcb->refcount++; //We have a thread waiting

//Then, as long as the thread isn't in the exited state...
  while(ptcb-> exited != 1){
    kernel_wait(&ptcb->exit_cv,SCHED_USER); //We wait for a thread to finish
    if(ptcb -> detached == 1){
      ptcb->refcount--; //The thread is no longer waiting
      return -1;
    }
  }
  ptcb->refcount--; //The thread is no longer waiting

  if (exitval != NULL)  
  {
    *exitval = ptcb->exitval; //If the exitval of a process exists, we update it with the one of our ptcb
  }
  if (ptcb->refcount == 0)  //As long as no threads are waiting..
  {
    rlist_remove(&ptcb->ptcb_list_node); //We remove it from the list
    free(ptcb);
  }
    return 0;
}



/**
  @brief Detach the given thread.
  */
int sys_ThreadDetach(Tid_t tid)
{
	PTCB* ptcb =NULL;
  //Searching our list based on the given tid
  if(rlist_find(&CURPROC->ptcb_list,(PTCB*)tid,NULL)!=NULL){
    ptcb=(PTCB*)tid;   //If we actually found it, we update our ptcb declared above to the one we found                      
  }
  if((ptcb==NULL) || (ptcb->exited==1)){           //We can't detach a NULL PTCB, nor an exited one
    return -1;
  }
  ptcb->detached=1;       //Given none of the above 2 checks are true, we go ahead and detach the PTCB

  kernel_broadcast(&ptcb->exit_cv); //Waking up all off our PTCBs



  return 0;
}

/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval)
{
  PCB *curproc=CURPROC;
  PTCB* ptcb= CURTHREAD->ptcb;
  ptcb->exited=1;                 //Setting the current thread to the exited state
  ptcb->exitval=exitval;
  CURPROC->thread_count--;

 
  kernel_broadcast(&ptcb->exit_cv);     //Waking up all of our PTCBs


  if(CURPROC->thread_count==0){     //If our process has no PTCBs

  /* Reparent any children of the exiting process to the 
       initial task */
    if(get_pid(CURPROC)!=1){

    PCB* initpcb = get_pcb(1);
    while(!is_rlist_empty(& curproc->children_list)) {
      rlnode* child = rlist_pop_front(& curproc->children_list);
      child->pcb->parent = initpcb;
      rlist_push_front(& initpcb->children_list, child);
    }

    /* Add exited children to the initial task's exited list 
       and signal the initial task */
    if(!is_rlist_empty(& curproc->exited_list)) {
      rlist_append(& initpcb->exited_list, &curproc->exited_list);
      kernel_broadcast(& initpcb->child_exit);
    }

    /* Put me into my parent's exited list */
    rlist_push_front(& curproc->parent->exited_list, &curproc->exited_node);
    kernel_broadcast(& curproc->parent->child_exit);

  }

  

  assert(is_rlist_empty(& curproc->children_list));
  assert(is_rlist_empty(& curproc->exited_list));


  /* 
    Do all the other cleanup we want here, close files etc. 
   */

  /* Release the args data */
  if(curproc->args) {
    free(curproc->args);
    curproc->args = NULL;
  }

  /* Clean up FIDT */
  for(int i=0;i<MAX_FILEID;i++) {
    if(curproc->FIDT[i] != NULL) {
      FCB_decref(curproc->FIDT[i]);
      curproc->FIDT[i] = NULL;
    }
  }

  /* Disconnect my main_thread */
  curproc->main_thread = NULL;

  /* Now, mark the process as exited. */
  curproc->pstate = ZOMBIE;

}

  /* Bye-bye cruel world */
  kernel_sleep(EXITED, SCHED_USER);
}

