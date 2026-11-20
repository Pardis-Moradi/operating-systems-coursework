#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "user/uthread.h"

extern void thread_switch(struct context*, struct context*);

struct thread all_thread[MAX_THREAD];
struct thread *current_thread;

uint64 ra;

volatile int a_started, b_started, c_started;
volatile int a_n, b_n, c_n;

void 
thread_init(void)
{
  current_thread = &all_thread[0];
  current_thread->state = RUNNING;
}

void 
thread_schedule(void)
{
  // TODO
  struct thread *t;
  struct thread *prev_thread = current_thread;

  int start_index = (prev_thread - all_thread + 1) % MAX_THREAD;
  int found_runnable = 0;

  for (int i = 0; i < MAX_THREAD; i++) {
    t = &all_thread[(start_index + i) % MAX_THREAD];
    if (t->state == RUNNABLE) {
      current_thread = t;
      found_runnable = 1;
      break;
    }
  }

  if (found_runnable) {
    current_thread->state = RUNNING;
    thread_switch(&prev_thread->context, &current_thread->context);
    ra = current_thread->context.ra;
  }

  else {
    if (prev_thread->state != RUNNING) {
      printf("thread_schedule: no runnable threads\n"); 
      exit(0);
    }
    if (prev_thread == &all_thread[0] && prev_thread->state == RUNNING) {
      int active_threads = 0;
      for (int i = 1; i < MAX_THREAD; i++) {
          if (all_thread[i].state != FREE) active_threads++;
      }
      if (active_threads == 0) {
        printf("thread_schedule: no runnable threads\n");
        exit(0);
      }
    }
  }
}

void thread_exit() {
    char name[16];
    int count;

    if (current_thread == &all_thread[1]) {
        strcpy(name, "thread_a");
        count = a_n;
    } else if (current_thread == &all_thread[2]) {
        strcpy(name, "thread_b");
        count = b_n;
    } else if (current_thread == &all_thread[3]) {
        strcpy(name, "thread_c");
        count = c_n;
    } else {
        strcpy(name, "unknown");
        count = 0;
    }
    
    printf("%s: exit after %d\n", name, count);
    current_thread->state = FREE; 
    thread_schedule(); 
    
    // Should never reach here
    printf("Error: thread_exit returned!\n");
    exit(1);
}

void 
thread_create(void (*func)())
{
  // TODO
  struct thread* t;
  for (t = all_thread; t < all_thread + MAX_THREAD; t++)
    if (t->state == FREE)
      break;
  if (t == all_thread + MAX_THREAD) {
    printf("Thread pool is full!");
    return;
  }
  t->state = RUNNABLE;
  uint64 stack_top = (uint64) t->stack + STACK_SIZE;
  uint64 *sp = (uint64 *)(stack_top & ~0xF);
  *(--sp) = (uint64)thread_exit;
  t->context.sp = (uint64)sp;
  t->context.ra = (uint64)func;
}

void 
thread_yield(void)
{
  // TODO
  if (current_thread == 0) return;
  if (current_thread->state == RUNNING)
    current_thread->state = RUNNABLE;
  thread_schedule();
}

#define THREAD_LOOP(name, count_var) \
  if (!name##_started) { \
    printf("thread_" #name " started\n"); \
    name##_started = 1; \
    thread_yield(); \
  } else if (count_var < 100) { \
    printf("thread_" #name " %d\n", count_var); \
    count_var++; \
    thread_yield(); \
  } else thread_exit(); \

void 
thread_a(void)
{
  // TODO
  THREAD_LOOP(a, a_n);
}

void 
thread_b(void)
{
  // TODO
  THREAD_LOOP(b, b_n);
}

void 
thread_c(void)
{
  // TODO
  THREAD_LOOP(c, c_n);
}

int 
main(int argc, char **argv[]) 
{
  a_started = b_started = c_started = 0;
  a_n = b_n = c_n = 0;
  thread_init();
  thread_create(thread_a);
  thread_create(thread_b);
  thread_create(thread_c);
  thread_schedule();
  exit(0);
}