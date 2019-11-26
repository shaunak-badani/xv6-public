#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

typedef struct proc_stat { 
    int pid; 
    float runtime;
    int num_run;
    int current_queue;
    int ticks[5];
} proc_stat; 

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;



// array of linked lists

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

#ifdef MLFQ
struct proc* queues[5][64];
int active_processes[5][64];
void delete_from_queue(int pid);
void add_to_queue(int queue_no, struct proc* p);
int find_no_of_processes(int queue_no);
#endif


void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);

  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  p->ctime = ticks;
  p->rtime = 0;
  p->etime = 0;
  p->priority = 60;
  p->num_run = 0;
  p->current_queue = 0;
  p->allowed_time = 0;
  p->sched_time = 0;
  for(int k = 0 ; k < 5 ; k++) p->ticks[k] = 0;
  #ifdef MLFQ
  int i;
  for(i = 0 ; active_processes[0][i] != 0 ; i++) {
    if(active_processes[0][i] == -1) {
      // replace inactive process with new active one
      break;
    }
  }
  queues[0][i] = p;
  active_processes[0][i] = 1;
  #endif

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");
  p->ctime=ticks;
  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;
  curproc->etime = ticks;
  #ifdef MLFQ
  delete_from_queue(curproc->pid);
  #endif

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->ctime=0;
        p->rtime=0;
        p->etime=0;
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

int
waitx(int *wtime, int *rtime)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        *rtime = p->rtime;
        *wtime = ticks - p->ctime - p->rtime;
        p->etime = 0;
        p->rtime = 0;
        p->ctime = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
        // return z;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.

#ifdef RR

void
scheduler(void)
{
  // Round Robin Basic Scheduling
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);
  }
}

#endif

#ifdef PBS
void
scheduler(void)
{
  // PBS
  struct proc *p, *j;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    struct proc* proc_acc = 0;
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;
      
      proc_acc = p;
      for(j = ptable.proc; j < &ptable.proc[NPROC] ; j++) {
        if(j->state != RUNNABLE) continue;
        if(proc_acc->priority > j->priority) proc_acc = j;
      }
      p = proc_acc;
      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);
  }
}
#endif

#ifdef FCFS
void
scheduler(void) {
  struct proc *p, *j;
  struct cpu *c = mycpu();
  c->proc = 0;
  cprintf("idhar abhi\n");

  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    struct proc *proc_acc = 0;
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;
      proc_acc = p;

      for(j = ptable.proc; j < &ptable.proc[NPROC] ; j++) {
        if(j->state != RUNNABLE) continue;
        if(proc_acc->ctime > j->ctime) proc_acc = j;

      }
      p = proc_acc;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;
      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);
  }
}
#endif

#ifdef MLFQ

int get_allowed_time(int q) {
  int base_ticks = BASE_TICKS;
  for(int i = 0 ; i < q ; i++) {
    base_ticks *= 2;
  }
  return base_ticks;
}

void
scheduler(void) {
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    int queue_no, j;

    queue_no = 0;
    while(queue_no < 5) {
      if(find_no_of_processes(queue_no) >= 0) break;
      queue_no = (queue_no + 1)%5;
    }
    // for(queue_no = 0 ; queue_no < 5; queue_no++,base_ticks *= 2) {
      for(j = 0 ; active_processes[queue_no][j] != 0 ; j++) {

        // WORKING MLFQ CODE
        p = queues[queue_no][j];
        if(p->state != RUNNABLE) continue;
        struct proc* p_min = p;
        if(p->pid == 1 || p->pid == 2) p_min = p;
        else{
          if(active_processes[queue_no][j] == -1) continue;
          // Switch to chosen process.  It is the process's job
          // to release ptable.lock and then reacquire it
          // before jumping back to us.
          if(queue_no < 4) {
            for(int m = 0 ; active_processes[queue_no][m] != 0 ; m++) {
              if(queues[queue_no][m]->state != RUNNABLE) continue; 
              if(p_min->ctime > queues[queue_no][m]->ctime) {
                p_min = queues[queue_no][m]; 
              }
            }
          }
        }

        p_min->sched_time = 0;


        c->proc = p_min;
        switchuvm(p_min);
        p_min->state = RUNNING;
        swtch(&(c->scheduler), p_min->context);
        switchkvm();

        if(p_min->sched_time > get_allowed_time(p_min->current_queue)) {
          if(p_min->pid < 3) continue;
          if(p->current_queue == 4) continue;
          delete_from_queue(p_min->pid);
          p_min->current_queue += 1;
          cprintf("shifted queue for id and new queue %d %d\n", p_min->pid, p_min->current_queue);
          add_to_queue(p_min->current_queue, p_min);
        }

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }
    // }
    // cprintf("runtime & id: %d %d\n", running_p->rtime, running_p->pid);        

    release(&ptable.lock);
  }
}
#endif

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      return 0;
      release(&ptable.lock);
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

void update_proc_time(int ticks_passed) {
  struct proc *p;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == RUNNING) p->rtime++;
  }
}


void update_sched_time() {
  #ifdef MLFQ
  struct proc* p;
  acquire(&ptable.lock);
  for(int queue_no = 0 ; queue_no < 5; queue_no++) {
    for(int j = 0 ; active_processes[queue_no][j] != 0 ; j++) {
      p = queues[queue_no][j];
      if(active_processes[queue_no][j] != -1 && p->state == RUNNING) {
        p->sched_time += 1;
        if(p->sched_time > 8) cprintf("more than 7\n");
      }
    }
  }
  release(&ptable.lock);
  return;
  #endif
}


void list_processes() {
  struct proc* p;

  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  char *state;

  #ifdef MLFQ
  cprintf("Proc State Id CreationTime RunTime Priority SchedTime currentQueue\n");
  #else
  cprintf("PROC STATE ID CREATTIME RUNTIME PRIORITY\n");
  #endif
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    #ifdef MLFQ
    cprintf("%s %s %d %d %d %d %d %d %d\n", p->name, state, p->pid , p->ctime, p->rtime, p->priority, p->sched_time, get_allowed_time(p->current_queue), p->current_queue);
    #else
    cprintf("%s %s %d %d %d %d\n", p->name, state, p->pid , p->ctime, p->rtime, p->priority);
    #endif
  }
  release(&ptable.lock);
}

int set_priority(int process_id, int priority) {
  struct proc *p;
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(process_id == p->pid) {
      p->priority = priority;
      break;
    }
  }
  release(&ptable.lock);
  
  return process_id;
}

#ifdef MLFQ

void getprocessinfo(int pid) {

  struct proc* p;
  acquire(&ptable.lock);
  for(int queue_no = 0 ; queue_no < 5; queue_no++) {
    for(int j = 0 ; active_processes[queue_no][j] != 0; j++) {
        p = queues[queue_no][j];
        if(p->pid == pid) {
          cprintf("name : %s\n", p->name);
          cprintf("runtime : %d\n", p->rtime);
          cprintf("current queue : %d\n", p->current_queue);
          break;
        }
    }
  }
  release(&ptable.lock);
}

void check_queues() {

  struct proc* p;
  char *state;

  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  for(int queue_no = 0 ; queue_no < 5; queue_no++) {
    int no_of_processes = 0;
    for(int j = 0 ; active_processes[queue_no][j] != 0; j++) {
      if(active_processes[queue_no][j] == -1) continue;
      p = queues[queue_no][j];
      if(p->state == UNUSED)
        continue;
      if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
        state = states[p->state];
      else
        state = "???";
      cprintf("%s %s %d %d %d %d %d %d\n", p->name, state, p->pid , p->ctime, p->rtime, p->priority, p->current_queue, get_allowed_time(p->current_queue));

      no_of_processes++;
    }
    cprintf("Queue %d - No Of Processes : %d\n", queue_no, no_of_processes);
  }
}

void delete_from_queue(int pid) {
  struct proc* p;
  for(int queue_no = 0 ; queue_no < 5; queue_no++) {
    for(int j = 0 ; active_processes[queue_no][j] != 0 ; j++) {
      p = queues[queue_no][j];
      if(p->pid == pid) {
        active_processes[queue_no][j] = -1;
        break;
      }
    }
  }
}

void add_to_queue(int queue_no, struct proc* p) {
  int j;
  for(j = 0 ; active_processes[queue_no][j] != 0 ; j++) {
    if(active_processes[queue_no][j] == -1) break;
  }
  queues[queue_no][j] = p;
  active_processes[queue_no][j] = 1;
}

// returns the number of processes runnable
int find_no_of_processes(int queue_no){
  int n = 0;
  if(queue_no == 1) cprintf("queue 1");
  for(int j = 0 ; active_processes[queue_no][j] != 0 ; j++) {
    if(active_processes[queue_no][j] == 1 && queues[queue_no][j]->state == RUNNABLE) {
      n++;
      // cprintf("proc_id %d queue no %d j %d \n",queues[queue_no][j]->pid, queue_no, j );
    }
  }
  return n;
}
#endif
