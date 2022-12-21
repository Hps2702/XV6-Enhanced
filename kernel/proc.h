// Saved registers for kernel context switches.
struct context
{
  uint64 ra;
  uint64 sp;

  // callee-saved
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};

// Per-CPU state.
struct cpu
{
  struct proc *proc;      // The process running on this cpu, or null.
  struct context context; // swtch() here to enter scheduler().
  int noff;               // Depth of push_off() nesting.
  int intena;             // Were interrupts enabled before push_off()?
};

extern int schedulingpolicy;
extern struct cpu cpus[NCPU];

// per-process data for the trap handling code in trampoline.S.
// sits in a page by itself just under the trampoline page in the
// user page table. not specially mapped in the kernel page table.
// uservec in trampoline.S saves user registers in the trapframe,
// then initializes registers from the trapframe's
// kernel_sp, kernel_hartid, kernel_satp, and jumps to kernel_trap.
// usertrapret() and userret in trampoline.S set up
// the trapframe's kernel_*, restore user registers from the
// trapframe, switch to the user page table, and enter user space.
// the trapframe includes callee-saved user registers like s0-s11 because the
// return-to-user path via usertrapret() doesn't return through
// the entire kernel call stack.
struct trapframe
{
  /*   0 */ uint64 kernel_satp;   // kernel page table
  /*   8 */ uint64 kernel_sp;     // top of process's kernel stack
  /*  16 */ uint64 kernel_trap;   // usertrap()
  /*  24 */ uint64 epc;           // saved user program counter
  /*  32 */ uint64 kernel_hartid; // saved kernel tp
  /*  40 */ uint64 ra;
  /*  48 */ uint64 sp;
  /*  56 */ uint64 gp;
  /*  64 */ uint64 tp;
  /*  72 */ uint64 t0;
  /*  80 */ uint64 t1;
  /*  88 */ uint64 t2;
  /*  96 */ uint64 s0;
  /* 104 */ uint64 s1;
  /* 112 */ uint64 a0;
  /* 120 */ uint64 a1;
  /* 128 */ uint64 a2;
  /* 136 */ uint64 a3;
  /* 144 */ uint64 a4;
  /* 152 */ uint64 a5;
  /* 160 */ uint64 a6;
  /* 168 */ uint64 a7;
  /* 176 */ uint64 s2;
  /* 184 */ uint64 s3;
  /* 192 */ uint64 s4;
  /* 200 */ uint64 s5;
  /* 208 */ uint64 s6;
  /* 216 */ uint64 s7;
  /* 224 */ uint64 s8;
  /* 232 */ uint64 s9;
  /* 240 */ uint64 s10;
  /* 248 */ uint64 s11;
  /* 256 */ uint64 t3;
  /* 264 */ uint64 t4;
  /* 272 */ uint64 t5;
  /* 280 */ uint64 t6;
};

#ifdef MLFQ
  #define WAITING_LIMIT 16
#endif
enum procstate
{
  UNUSED,
  USED,
  SLEEPING,
  RUNNABLE,
  RUNNING,
  ZOMBIE
};
enum queued 
{
  QUEUED, 
  NOTQUEUED
};
// struct Queue{
//     int head;
//     int tail;
//     int capacity;
//     struct proc* array[QSIZE];
// };

// Per-process state
struct proc
{
  struct spinlock lock;

  // p->lock must be held when using these:
  enum procstate state; // Process state
  void *chan;           // If non-zero, sleeping on chan
  int killed;           // If non-zero, have been killed
  int xstate;           // Exit status to be returned to parent's wait
  int pid;              // Process ID

  // wait_lock must be held when using this:
  struct proc *parent; // Parent process

  // these are private to the process, so p->lock need not be held.
  uint64 kstack;               // Virtual address of kernel stack
  uint64 sz;                   // Size of process memory (bytes)
  pagetable_t pagetable;       // User page table
  struct trapframe *trapframe; // data page for trampoline.S
  struct context context;      // swtch() here to run process
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
  int mask;

  int nticks;
  int ticks_curr;
  struct trapframe *alarm_trapframe; // cache the trapframe when timer fires
  int alarm_work;
  int q_level;
  int q_rtime;
  int q_etime;
  int q_array[QCOUNT];
  uint rtime; // How long the process ran for
  uint ctime; // When was the process created
  uint etime; // When did the process exited
  uint ntickets;
  uint times_scheduled;
  enum queued queue_state;
  uint64 handler;
  uint64 nprocesses;
  uint64 stat_p;
  uint64 ticks_running;
  uint64 ticks_sleeping;
  uint64 ticks_total;
  uint64 start;
  uint64 end;
};


struct Queue{
    int head;
    int tail;
    struct proc* array[QSIZE];
};

extern struct Queue queuetable[QCOUNT];
void push(struct Queue* q, struct proc* p);
struct proc* pop(struct Queue* q);
void remove(struct Queue* q, struct proc* p);
int empty(struct Queue q);
//void push(struct Queue* q, struct proc* p);
// struct proc* pop(struct Queue* q);
// void remove(struct Queue* q, struct proc* p);
// int empty(struct Queue *q);


struct ptable_type {
  struct spinlock lock;
  struct proc proc[NPROC];
};
extern struct ptable_type ptable;

void set_process_tickets(int n);
void seed_gen_random(unsigned long);
long gen_random(void);
long random_mod(long);
