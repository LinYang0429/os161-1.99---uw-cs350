#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include <synch.h>
#include <mips/trapframe.h>
#include <vfs.h>
#include <vm.h>
#include <limits.h>
#include "opt-A2.h"

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  (void)exitcode;

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();

  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

#if OPT_A2
  p->exit = true;
  p->exitcode = _MKWAIT_EXIT(exitcode);

  lock_acquire(p->lk);
  cv_broadcast(p->cv, p->lk);
  lock_release(p->lk);
  
  unsigned count = array_num(p->p_children);
  for(unsigned i = 0; i < count; i++){
    struct proc * child = array_get(p->p_children, i);
    lock_release(child->wait_lk);
  }
#endif

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  proc_destroy(p);
  
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
#if OPT_A2
  *retval = curproc->pid;
#else
  *retval = 1;
#endif
  return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */

  if (options != 0) {
    return(EINVAL);
  }

#if OPT_A2
  struct proc * proc = get_child_proc(pid);
  if(proc == NULL) {
    for(int i = 0; i < n; i++){
	if(allProc[i] == pid){
        *retval = pid;
        return(0);
      }
    }
    return ESRCH;
  }
  if(proc == curproc) return ECHILD;

  lock_acquire(proc->lk);
  while (!proc->exit) {
    cv_wait(proc->cv, proc->lk);
  }
  lock_release(proc->lk);
  exitstatus = proc->exitcode;
#else
  /* for now, just pretend the exitstatus is 0 */
  exitstatus = 0;
#endif
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}

#if OPT_A2
int
sys_fork(struct trapframe *tf,
		pid_t *retval)
{
  KASSERT(curproc != NULL);
  struct proc * new = proc_create_runprogram(curproc->p_name);
  if(new == NULL)
  {
    return ENPROC;
  }
  as_copy(curproc_getas(), &(new->p_addrspace));
  if(new->p_addrspace == NULL)
  {
    proc_destroy(new);
    return ENOMEM;
  }
  struct trapframe *newtf = kmalloc(sizeof(struct trapframe));
  if(newtf == NULL)
  {
    proc_destroy(new);
    return ENOMEM;
  }
  memcpy(newtf, tf, sizeof(struct trapframe));
  new->p_parent = curproc;
  array_add(curproc->p_children, new, NULL);
  if(thread_fork(curthread->t_name, new, &enter_forked_process, newtf, 0))
  {
    proc_destroy(new);
    kfree(newtf);
    newtf = NULL;
    return ENOMEM;
  }
  lock_acquire(new->wait_lk);
  *retval = new->pid;
  return 0;
}

int 
sys_execv(const_userptr_t program, userptr_t args[], int *retval)
{
	int result;
	size_t total_len = 0;
	unsigned argc = 0;
	//count args number and args total length
	for(argc = 0; args[argc] != NULL; ++argc){
		total_len += strlen(((char**)args)[argc]) + 1;
	}
	if(total_len > ARG_MAX){
		return E2BIG;
	}
	if(argc > ARG_MAX){
		return E2BIG;
        }
	// copy prog name to kernel as
	size_t prog_len = strlen((char *)program) + 1;
	char kprog[prog_len];
	result = copyinstr(program, kprog, prog_len, NULL);
	if(result){
		return result;
	}
	// copy prog args to kernel as
	char kallargs[total_len];
	char *kargs[argc + 1];
	if(kargs == NULL){
		return ENOMEM;
	}
	unsigned count = 0;
	for(unsigned i = 0; i < argc; i++){
		size_t len = strlen(((char **) args)[i]) + 1;
		size_t cur;
		kargs[i] = kallargs + count;
		result = copyinstr(args[i], kargs[i], len, &cur);
		if(result) return result;
		count += cur;
	}
	kargs[argc] = NULL;
	// open the file
	char * progname = kstrdup(kprog);
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	result = vfs_open(progname, 0, 0, &v);
        if (result) {
                return result;
        }
	// created new as, replace old and activate new as
        KASSERT(curproc_getas() == NULL);
	struct addrspace *as;
	as = as_create();
        if (as ==NULL) {
                vfs_close(v);
                return ENOMEM;
        }
	curproc_setas(as);
        as_activate();
        // Load the executable.
        result = load_elf(v, &entrypoint);
        if (result) {
                vfs_close(v);
                return result;
        } 
        // Done with the file now.
        vfs_close(v);
        // Define the user stack in the address space
        result = as_define_stack(as, &stackptr);
        if (result) {
                return result;
        }
	// copy args to user stack as
	char *argv[argc + 1];	
	for(unsigned j = 0; j < argc; j++){
		size_t len = strlen(((char **) kargs)[j]) + 1;
		stackptr -= len;
		result = copyout(kargs[j], (userptr_t)stackptr, len);
		if(result) return result;
	}
	argv[argc] = NULL;
	result = copyout(argv, (userptr_t)stackptr, (sizeof(char *) * (argc + 1)));
	if(result) return result;

	kfree(progname);

	// Warp to user mode.
        enter_new_process(0 /*argc*/, NULL /*userspace addr of argv*/,
                          stackptr, entrypoint);

        //enter_new_process does not return.
        panic("enter_new_process returned\n");
	*retval = EINVAL;
        return EINVAL;
}
#endif
