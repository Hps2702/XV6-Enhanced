#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
int total_tickets = 0;
struct Queue queuetable[QCOUNT];
struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;
#ifdef FCFS
int schedulingpolicy = 1;
#endif
#ifdef PBS
int schedulingpolicy = 2;
#endif
#ifdef LBS
int schedulingpolicy = 3;
#endif
#ifdef MLFQ
int schedulingpolicy = 4;
#endif
#ifndef FCFS
#ifndef PBS
#ifndef LBS
#ifndef MLFQ
int schedulingpolicy = 0;
#endif
#endif
#endif
#endif
// If no scheduling policy is defined, or a non-existing policy is specified

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S
int DEFAULT_TICKETS = 1;  // used for the random sched

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;
#define N 624
#define M 397
#define MATRIX_A 0x9908b0df   /* constant vector a */
#define UPPER_MASK 0x80000000 /* most significant w-r bits */
#define LOWER_MASK 0x7fffffff /* least significant r bits */

/* Tempering parameters */   
#define TEMPERING_MASK_B 0x9d2c5680
#define TEMPERING_MASK_C 0xefc60000
#define TEMPERING_SHIFT_U(y)  (y >> 11)
#define TEMPERING_SHIFT_S(y)  (y << 7)
#define TEMPERING_SHIFT_T(y)  (y << 15)
#define TEMPERING_SHIFT_L(y)  (y >> 18)

#define RAND_MAX 0x7fffffff

static unsigned long mt[N]; /* the array for the state vector  */
static int mti=N+1; /* mti==N+1 means mt[N] is not initialized */

/* initializing the array with a NONZERO seed */
void
seed_gen_random(unsigned long seed)
{
    /* setting initial seeds to mt[N] using         */
    /* the generator Line 25 of Table 1 in          */
    /* [KNUTH 1981, The Art of Computer Programming */
    /*    Vol. 2 (2nd Ed.), pp102]                  */
    mt[0]= seed & 0xffffffff;
    for (mti=1; mti<N; mti++)
        mt[mti] = (69069 * mt[mti-1]) & 0xffffffff;
}

long /* for integer generation */
gen_random()
{
    unsigned long y;
    static unsigned long mag01[2]={0x0, MATRIX_A};
    /* mag01[x] = x * MATRIX_A  for x=0,1 */

    if (mti >= N) { /* generate N words at one time */
        int kk;

        if (mti == N+1)   /* if sgenrand() has not been called, */
            seed_gen_random(4357); /* a default initial seed is used   */

        for (kk=0;kk<N-M;kk++) {
            y = (mt[kk]&UPPER_MASK)|(mt[kk+1]&LOWER_MASK);
            mt[kk] = mt[kk+M] ^ (y >> 1) ^ mag01[y & 0x1];
        }
        for (;kk<N-1;kk++) {
            y = (mt[kk]&UPPER_MASK)|(mt[kk+1]&LOWER_MASK);
            mt[kk] = mt[kk+(M-N)] ^ (y >> 1) ^ mag01[y & 0x1];
        }
        y = (mt[N-1]&UPPER_MASK)|(mt[0]&LOWER_MASK);
        mt[N-1] = mt[M-1] ^ (y >> 1) ^ mag01[y & 0x1];

        mti = 0;
    }
  
    y = mt[mti++];
    y ^= TEMPERING_SHIFT_U(y);
    y ^= TEMPERING_SHIFT_S(y) & TEMPERING_MASK_B;
    y ^= TEMPERING_SHIFT_T(y) & TEMPERING_MASK_C;
    y ^= TEMPERING_SHIFT_L(y);

    // Strip off uppermost bit because we want a long,
    // not an unsigned long
    return y & RAND_MAX;
}

// Assumes 0 <= max <= RAND_MAX
// Returns in the half-open interval [0, max]
long random_mod(long max) {
  unsigned long
    // max <= RAND_MAX < ULONG_MAX, so this is okay.
    num_bins = (unsigned long) max + 1,
    num_rand = (unsigned long) RAND_MAX + 1,
    bin_size = num_rand / num_bins,
    defect   = num_rand % num_bins;

  long x;
  do {
   x = gen_random();
  }
  // This is carefully written not to overflow
  while (num_rand - defect <= (unsigned long)x);

  // Truncated division is intentional
  return x/bin_size;
}
// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    char *pa = kalloc();
    if (pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int)(p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void procinit(void)
{
  struct proc *p;

  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for (p = proc; p < &proc[NPROC]; p++)
  {
    initlock(&p->lock, "proc");
    p->state = UNUSED;
    p->kstack = KSTACK((int)(p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu *
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc *
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}
void set_process_tickets(int n)
{
  acquire(&proc->lock);
  total_tickets -= proc->ntickets;
  proc->ntickets = n;
  total_tickets += proc->ntickets;
  release(&proc->lock);
}

int allocpid()
{
  int pid;

  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc *
allocproc(void)
{
  struct proc *p;
  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->state == UNUSED)
    {
      goto found;
    }
    else
    {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;
  p->nprocesses = 0;
  p->stat_p = 60;
  p->ticks_running = 0;
  p->ticks_sleeping = 0;
  p->start = 0;
  p->ticks_curr = 0;


  // Allocate a trapframe page.
  if ((p->trapframe = (struct trapframe *)kalloc()) == 0)
  {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if (p->pagetable == 0)
  {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;
  p->rtime = 0;
  p->etime = 0;
  p->ctime = ticks;
#ifdef MLFQ
p->q_etime=ticks;
p->q_level=0;
for(int i=0;i<5;i++) p->q_array[i]=0;
#endif
  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if (p->trapframe)
    kfree((void *)p->trapframe);
  p->trapframe = 0;
  if (p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if (pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if (mappages(pagetable, TRAMPOLINE, PGSIZE,
               (uint64)trampoline, PTE_R | PTE_X) < 0)
  {
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if (mappages(pagetable, TRAPFRAME, PGSIZE,
               (uint64)(p->trapframe), PTE_R | PTE_W) < 0)
  {
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
    0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
    0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
    0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
    0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
    0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
    0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00};

// Set up first user process.
void userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;

  // allocate one user page and copy initcode's instructions
  // and data into it.
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;     // user program counter
  p->trapframe->sp = PGSIZE; // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

#ifdef RANDOMTICKETTRUE
  if (random() % 2)
  {
    // printf("%d", random() % 2);
    DEFAULT_TICKETS = 100;
  }
  else
  {
    DEFAULT_TICKETS = 1;
  }
#else
#endif

  p->ntickets = DEFAULT_TICKETS; // used for the RANDOM scheduler

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if (n > 0)
  {
    if ((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0)
    {
      return -1;
    }
  }
  else if (n < 0)
  {
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0)
  {
    return -1;
  }

  //np->mask = p->mask;

  // Copy user memory from parent to child.
  if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0)
  {
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

#ifdef RANDOMTICKETTRUE
  if (random() % 2)
  {
    // printf("%d", random() % 2);
    DEFAULT_TICKETS = 100;
  }
  else
  {
    DEFAULT_TICKETS = 1;
  }
#else
#endif

  np->ntickets = DEFAULT_TICKETS; // used in random sched

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for (i = 0; i < NOFILE; i++)
    if (p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->mask = p->mask;
  np->times_scheduled = p->times_scheduled;
  total_tickets -= np->ntickets;
  np->ntickets = p->ntickets;
  total_tickets += np->ntickets;
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void reparent(struct proc *p)
{
  struct proc *pp;

  for (pp = proc; pp < &proc[NPROC]; pp++)
  {
    if (pp->parent == p)
    {
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void exit(int status)
{
  struct proc *p = myproc();

  if (p == initproc)
    panic("init exiting");

  // Close all open files.
  for (int fd = 0; fd < NOFILE; fd++)
  {
    if (p->ofile[fd])
    {
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);

  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;
  p->etime = ticks;
  total_tickets -= p->ntickets;
  p->ntickets = 0;
  total_tickets += p->ntickets;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for (;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
    for (pp = proc; pp < &proc[NPROC]; pp++)
    {
      if (pp->parent == p)
      {
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if (pp->state == ZOMBIE)
        {
          // Found one.
          pid = pp->pid;
          if (addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                   sizeof(pp->xstate)) < 0)
          {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || killed(p))
    {
      release(&wait_lock);
      return -1;
    }

    // Wait for a child to exit.
    sleep(p, &wait_lock); // DOC: wait-sleep
  }
}

int set_priority(int priority, int pid)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);

    if (p->pid == pid)
    {
      int val = p->stat_p;
      p->stat_p = priority;

      p->ticks_running = 0;
      p->ticks_sleeping = 0;

      release(&p->lock);

      if (val > priority)
        yield();
      return val;
    }
    release(&p->lock);
  }
  return -1;
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int waitx(uint64 addr, uint *wtime, uint *rtime)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for (;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
    for (np = proc; np < &proc[NPROC]; np++)
    {
      if (np->parent == p)
      {
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if (np->state == ZOMBIE)
        {
          // Found one.
          pid = np->pid;
          *rtime = np->rtime;
          *wtime = np->etime - np->ctime - np->rtime;
          if (addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                   sizeof(np->xstate)) < 0)
          {
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || p->killed)
    {
      release(&wait_lock);
      return -1;
    }

    // Wait for a child to exit.
    sleep(p, &wait_lock); // DOC: wait-sleep
  }
}
void queuetableinit(void)
{
  for (int i = 0; i < QCOUNT; i++)
  {
    queuetable[i].head = 0;
    queuetable[i].tail = 0;
  }
}

void update_time()
{
  struct proc *p;
  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->state == RUNNING)
    {
      p->rtime++;
    }
    if(p->state==SLEEPING)
     p->ticks_sleeping++;
    release(&p->lock);
  }
}

int getpreempted(int level)
{
  for (int i = 0; i < level; i++)
  {
    if (!empty(queuetable[i]))
    {
      return 1;
    }
  }
  return 0;
}

int dynpro(struct proc *p)
{
  int niceness = 5;

  if (p->nprocesses)
  {
    if (p->ticks_sleeping + p->ticks_running != 0)
      niceness = (p->ticks_sleeping / (p->ticks_sleeping + p->ticks_running)) * 10;
    else
      niceness = 5;
  }
  return p->stat_p - niceness + 5;
}
void scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  // findmode();
  c->proc = 0;
  // if (schedulingpolicy == 1)
  //   printf("FCFS\n");
  // if (schedulingpolicy == 2)
  //   printf("PBS\n");
  // if (schedulingpolicy == 3)
  //   printf("LBS\n");
  // if (schedulingpolicy == 4)
  //   printf("MLFQ\n");

  // else
  //   printf("RR\n");
  for (;;)
  {
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

  #ifdef RR
      // printf("RR\n");
      for (p = proc; p < &proc[NPROC]; p++)
      {
        acquire(&p->lock);
        if (p->state == RUNNABLE)
        {
          // Switch to chosen process.  It is the process's job
          // to release its lock and then reacquire it
          // before jumping back to us.
          p->state = RUNNING;
          c->proc = p;

          p->nprocesses++;
          swtch(&c->context, &p->context);

          // Process is done running for now.
          // It should have changed its p->state before coming back.
          c->proc = 0;
        }
        release(&p->lock);
      }
    #endif

#ifdef FCFS
      // printf("FCFS\n");
      struct proc *process = 0;

      for (p = proc; p < &proc[NPROC]; p++)
      {
        int flag=0;
        acquire(&p->lock);
        if (p->state == RUNNABLE)
        {
          if (!process)
          {
            process = p;
           // continue;
           flag=1;
          }
          else
          {
            if (p->ctime < process->ctime)
            {
              release(&process->lock);
              process = p;
             // continue;
             flag=1;
            }
          }
        }
       if(!flag) release(&p->lock);
      }
      if (!process)
        continue;
      if (process)
      {
        process->state = RUNNING;
        c->proc = process;
        p->nprocesses++;
        swtch(&c->context, &process->context);
        c->proc = 0;
        release(&process->lock);
      }
    #endif
   #ifdef PBS
      // printf("PBS");
      struct proc *process = 0;
      int dp = 101;
      for (p = proc; p < &proc[NPROC]; p++)
      {
        acquire(&p->lock);
        int val = dynpro(p);
        int processDp = 0;
        int tmp = 100;
        if (val < 100)
          tmp = val;
        if (tmp >= 0)
          processDp = tmp;
        int flag1 = (dp == processDp && p->nprocesses < process->nprocesses);
        int flag2 = (dp == processDp && p->nprocesses == process->nprocesses && p->ctime < process->ctime);
        if (p->state == RUNNABLE)
        {
          if (!process)
          {
            process = p;
            dp = processDp;
            continue;
          }
          else
          {
            if (dp > processDp || flag1 || flag2)
            {
              release(&process->lock);
              process = p;
              dp = processDp;
              continue;
            }
          }
        }
        release(&p->lock);
      }

      if (process)
      {
        process->nprocesses++; //
        process->start = ticks;
        process->state = RUNNING;              //
        c->proc = process;                     //
        swtch(&c->context, &process->context); //
        c->proc = 0;                           //
        release(&process->lock);               //
      }
    #endif
    #ifdef LBS
    
      const int golden_ticket = (gen_random() % (100)) + 1;
      int ticket_count = 0;

      // Loop over process table looking for process to run.
      for (p = proc; p < &proc[NPROC]; p++)
      {
        acquire(&p->lock);
        if (p->state != RUNNABLE)

        {
          ticket_count += p->ntickets;
          release(&p->lock);
          continue;
        }

        ticket_count += p->ntickets;
        if (ticket_count < golden_ticket)
        {
          release(&p->lock);
          continue;
        }
        else if (ticket_count > total_tickets)
          p->state = RUNNING;
        c->proc = p;

        // const int tickstart = ticks;
        swtch(&c->context, &p->context);
        c->proc = 0;
        release(&p->lock);
        break;
      }

    #endif
  #ifdef MLFQ
      int nqueue=5;
       struct proc *process = 0;
       for(p=proc ; p < &proc[NPROC];p++){
         if(p->state==RUNNABLE){
           if((ticks - p ->q_etime > WAITING_LIMIT)&& p->q_level > 0){
              acquire(&p->lock);
              p->q_array[p->q_level]=p->q_array[p->q_level]+(ticks - p->q_etime);
              p->q_etime =ticks;
              p->q_level --;
              release(&p->lock);
           }

         }
       }
       for(p=proc ; p<&proc[NPROC];p++){
         if(p->state==RUNNABLE) {
           if(p->q_level < nqueue || process==0) {
             process=p;
             nqueue = process ->q_level;
           }
           else if(p->q_level==nqueue && p->q_etime < process->q_etime) process=p;

         }

       }
       if(process!=0){
         acquire(&process->lock);
         if(process->state==RUNNABLE){
           process->q_etime=ticks;
           process->nprocesses++;
           process->state=RUNNING;
           c->proc=process;
           swtch (&c->context,&process->context);
           process->q_array[process->q_level]=process->q_array[process->q_level] +(ticks - process ->q_etime);
           c->proc=0;
         }
         release (&process->lock);
       }


    #endif
}
}
void sched(void)
{
  int intena;
  struct proc *p = myproc();

  if (!holding(&p->lock))
    panic("sched p->lock");
  if (mycpu()->noff != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched running");
  if (intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first)
  {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock); // DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;
  total_tickets-=p->ntickets;
  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void wakeup(void *chan)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    if (p != myproc())
    {
      acquire(&p->lock);
      if (p->state == SLEEPING && p->chan == chan)
      {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int kill(int pid)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->pid == pid)
    {
      p->killed = 1;
      if (p->state == SLEEPING)
      {
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int killed(struct proc *p)
{
  int k;

  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if (user_dst)
  {
    return copyout(p->pagetable, dst, src, len);
  }
  else
  {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if (user_src)
  {
    return copyin(p->pagetable, dst, src, len);
  }
  else
  {
    memmove(dst, (char *)src, len);
    return 0;
  }
}
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;
#if defined RR
  printf("\nPID\tState\tpname\trtime\twait_time");
#endif
#ifdef FCFS
  printf("\nPID\tState\trtime\tctime");
#endif
#ifdef LBS
  printf("\nPID\tState\tTickets");
#endif
#ifdef PBS
  // printf("\nPID\tPriority\tState\trunning_time\twait_time\tno_of_runss");
  printf("PID\tPriority\tState\trtime\twait_time\tno_of_runs");
#endif
#ifdef MLFQ
  printf("\nPID\tP\tState\trtime\twait_time\tno_of_runs\tq0\tq1\tq2\tq3\tq4");
#endif
  printf("\n");

  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
        #if defined RR
        int end_time=p->etime;
        if(end_time==0) end_time=ticks;
     printf("%d\t%s\t%s\t%d\t%d", p->pid, state, p->name,p->rtime,end_time-p->ctime-p->rtime);
    printf("\n");
  #endif
  #ifdef FCFS
   int end_time=p->etime;
        if(end_time==0) end_time=ticks;
        printf("%d\t%s\t%d\t%d",p->pid,state,p->rtime,p->ctime);
        printf("\n");
  #endif
  #ifdef LBS
  printf("%d\t%s\t%d",p->pid,state,p->ntickets);
  printf("\n");
  #endif
  #ifdef PBS
  int end_time=p->etime;
        if(end_time==0) end_time=ticks;
    //      int niceness = 5;
    // if (p->ticks_sleeping+ p->ticks_running != 0)
    //   niceness = (int)((p->ticks_sleeping / (p->ticks_sleeping + p->ticks_running)) * 10);
    // int value = (p->stat_p - niceness + 5 < 100 ? p->stat_p - niceness + 5 : 100);
    int gg=dynpro(p);
    int value=100;
    if(gg<100) value=gg;
    int dp = (0 < value ? value : 0);

    printf("%d\t%d\t\t%s\t%d\t%d\t\t%d", p->pid, dp, state, p->ticks_running, end_time - p->ctime - p->ticks_running, p->ticks_running+p->ticks_sleeping);
    printf("\n");
  #endif
 #ifdef MLFQ
    int end_time = p->etime;
    if (end_time == 0)
      end_time = ticks;
int Queue_no=p->q_level;
if(p->state==ZOMBIE) Queue_no=-1;
  // printf("%d\t%s\t%d\t%d\t%d\t", p->pid, state, p->rtime, end_time - p->ctime - p->rtime, p->nprocesses);
     printf("%d\t%d\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d", p->pid, Queue_no, state, p->rtime, end_time - p->ctime - p->rtime, p->nprocesses, p->q_array[0], p->q_array[1], p->q_array[2], p->q_array[3], p->q_array[4]);
    // printf("\n");
    printf("\n");
#endif
  }

}