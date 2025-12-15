// Kernel functions
// J Losh

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target uC:       TM4C123GH6PM
// System Clock:    40 MHz

#ifndef KERNEL_H_
#define KERNEL_H_

#include <stdint.h>
#include <stdbool.h>

//-----------------------------------------------------------------------------
// RTOS Defines
//-----------------------------------------------------------------------------

typedef void (*_fn)();

// ------------------ Mutex ------------------
#define MAX_MUTEXES 1
#define MAX_MUTEX_QUEUE_SIZE 2
#define resource 0

typedef struct _mutex
{
    bool lock;
    uint8_t queueSize;
    uint8_t processQueue[MAX_MUTEX_QUEUE_SIZE];
    uint8_t lockedBy;
} mutex;

// ------------------ Semaphore ------------------
#define MAX_SEMAPHORES 3
#define MAX_SEMAPHORE_QUEUE_SIZE 2
#define keyPressed 0
#define keyReleased 1
#define flashReq 2

typedef struct _semaphore
{
    uint8_t count;
    uint8_t queueSize;
    uint8_t processQueue[MAX_SEMAPHORE_QUEUE_SIZE];
} semaphore;

// ------------------ Tasks ------------------
#define MAX_TASKS 12

#define STATE_INVALID           0
#define STATE_UNRUN             1
#define STATE_READY             2
#define STATE_DELAYED           3
#define STATE_BLOCKED_SEMAPHORE 4
#define STATE_BLOCKED_MUTEX     5
#define STATE_KILLED            6

// ------------------ Task Control Block ------------------
struct _tcb
{
    uint8_t state;
    void *pid;
    void *sp;
    uint8_t priority;
    uint8_t currentPriority;
    uint32_t ticks;
    uint64_t srd;
    char name[16];
    uint8_t mutex;
    uint8_t semaphore;
    uint32_t cpuTime;
    uint16_t percentCPU;
    uint32_t lastStartTime;
    uint32_t runTime;
    uint32_t cpuPercent;
    void    *stackBase;
    uint32_t stackSize;
};

// ------------------ Global Kernel Objects ------------------
extern struct _tcb tcb[MAX_TASKS];
extern uint8_t taskCurrent;
extern mutex mutexes[MAX_MUTEXES];
extern semaphore semaphores[MAX_SEMAPHORES];
extern bool preemption;
extern bool priorityScheduler;
extern bool priorityInheritance;

// ------------------ Kernel API ------------------
bool initMutex(uint8_t mutex);
bool initSemaphore(uint8_t semaphore, uint8_t count);

void initRtos(void);
void startRtos(void);

bool createThread(_fn fn, const char name[], uint8_t priority, uint32_t stackBytes);
void killThread(_fn fn);
void restartThread(_fn fn);
void setThreadPriority(_fn fn, uint8_t priority);

void yield(void);
void lock(int8_t mutex);
void unlock(int8_t mutex);

void sysTickIsr(void);
void svCallIsr(void);

#endif

