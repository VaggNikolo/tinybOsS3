
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"
#include "kernel_cc.h"
#include "kernel_streams.h"



void begin_thread(){
  int exitval;

  Task call = CURTHREAD->ptcb->task;
  int argl = CURTHREAD->ptcb->argl;
  void* args = CURTHREAD->ptcb->args;

  exitval = call(argl,args);
  ThreadExit(exitval);
}

/** 
  @brief Create a new thread in the current process.
  */
Tid_t sys_CreateThread(Task task, int argl, void* args)
{
	if (task!=NULL){
  PTCB* ptcb = (PTCB*)xmalloc(sizeof(PTCB));     //aquiring a PTCB
  ptcb -> task = task;                           //initializing its values
  ptcb -> argl = argl;
  ptcb->args=(args == NULL)?NULL:args;

  ptcb -> exitval = CURPROC -> exitval;
  ptcb -> exited = 0;
  ptcb -> detached = 0;
  ptcb -> refcount = 0 ;
  ptcb -> exit_cv = COND_INIT;



  CURPROC -> thread_count ++;

  CURTHREAD->ptcb = ptcb;

  TCB* tcb = spawn_thread(CURPROC,begin_thread); //create a tcb

  ptcb -> tcb = tcb;  //we link tcb with ptcb
  tcb -> ptcb = ptcb; //we link ptcb with tcb
  ptcb->tcb->owner_pcb = tcb->owner_pcb;

  rlnode_init(&ptcb-> ptcb_list_node,ptcb); //initialize a ptcb list node 
  rlist_push_back(&CURPROC->ptcb_list,&ptcb->ptcb_list_node);// place it in the last pos of the thread list (ptcb list)

  wakeup(ptcb->tcb); //wake up tcb
  return (Tid_t) ptcb;

  }
  return NOTHREAD;
}

/**
  @brief Return the Tid of the current thread.
 */
Tid_t sys_ThreadSelf()
{
	return (Tid_t) CURTHREAD->ptcb;
}

/**
  @brief Join the given thread.
  */
int sys_ThreadJoin(Tid_t tid, int* exitval)
{
	PTCB* ptcb = (PTCB*)tid;
  if(tid == NOTHREAD || (rlist_find(&CURPROC->ptcb_list,ptcb,0)) == NULL || tid == sys_ThreadSelf()){ //search the ptcb_list for the given tid (see if it exists)
      return -1;
  }
  
  ptcb->refcount++;

  while(ptcb-> exited == 0 && ptcb -> detached == 0 ){
    kernel_wait(&ptcb->exit_cv,SCHED_USER); // we wait for a thread to terminate
  }

  ptcb->refcount--;

  if(ptcb-> detached == 1){  //cannot join an exited or detached ptcb      
    return -1;
  }



  if(ptcb->exited == 1) {
    if(exitval!=NULL){
    *exitval=ptcb->exitval;
    }
    if(ptcb->refcount == 0)
    {
    rlist_remove(&ptcb->ptcb_list_node);//remove from from list
    free(ptcb);
    }

  }

  return 0;
}

/**
  @brief Detach the given thread.
  */
int sys_ThreadDetach(Tid_t tid)
{
	PTCB* ptcb =NULL;
  if(rlist_find(&CURPROC->ptcb_list,(PTCB*)tid,NULL)){ //search the ptcb_list for the given tid (see if it exists)
    ptcb=(PTCB*)tid;                                    //
  }
  if(ptcb==NULL){           //cannot detach a null ptcb
    return -1;
  }
  if(ptcb->exited==1){      //cannot detach an exited ptcb
    return -1;
  }
  ptcb->detached=1;       //set to detached


  kernel_broadcast(&ptcb->exit_cv); //wakeup all waiting ptcbs



  return 0;
}

/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval)
{
  PTCB* ptcb= CURTHREAD->ptcb;
  ptcb->exited=1;                 //set to exited
  ptcb->exitval=exitval;
  CURPROC->thread_count--;

 
  kernel_broadcast(&ptcb->exit_cv);     //wakeup all waiting ptcbs
    

  PCB *curproc=CURPROC;

///
  if(CURPROC->thread_count==0){     //if the pcb has no ptcbs

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

