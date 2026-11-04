#ifndef TASKS_H
#define TASKS_H
#include <stdint.h>
typedef enum {TS_UNUSED,TS_NEW,TS_READY,TS_RUNNING,TS_WAITING,TS_TERMINATED} thread_state_t;
/* Resumen para la ventana Sistema: procesos vivos, hilos y despachos. */
typedef struct {unsigned processes,threads;uint64_t dispatches;} tasks_info_t;
tasks_info_t tasks_info(void);
void tasks_init(void);
void tasks_poll(void);
void tasks_command(const char *arg);
uint64_t process_create(const char *name);
uint64_t thread_create(uint64_t pid,const char *name,void (*entry)(void *),void *arg);
void thread_yield(void);
void thread_sleep(uint64_t ticks);
void thread_exit(void) __attribute__((noreturn));
void process_list(void);
void thread_list(void);
void scheduler_command(const char *arg);
#endif
