#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

void
push(struct Queue* q, struct proc* p) {
  q->array[q->head++] = p;
  q->head %= QSIZE;
  if (q->head == q->tail) {
    panic("Full queue push");
  }
  p->queue_state = QUEUED;
}

struct proc*
pop(struct Queue* q)
{
  if (q->tail == q->head) {
    panic("Empty queue pop");
  }
  struct proc* p = q->array[q->tail];
  p->queue_state = NOTQUEUED;
  q->tail++;
  q->tail %= QSIZE;
  return p;
}

void
remove(struct Queue* q, struct proc* p) {
  if (p->queue_state == NOTQUEUED) return;
  for (int i = q->tail; i != q->head; i = (i + 1) % QSIZE) {
    if (q->array[i] == p) {
      p->queue_state = NOTQUEUED;
      for (int j = i + 1; j != q->head; j = (j + 1) % QSIZE) {
        q->array[(j - 1 + QSIZE) % QSIZE] = q->array[j];
      }
      q->head = (q->head - 1 + QSIZE) % QSIZE;
      break;
    }
  }
}

int
empty(struct Queue q) {
  return (q.head - q.tail + QSIZE) % QSIZE == 0;
}
