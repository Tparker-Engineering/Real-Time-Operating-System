// Kernel functions
// J Losh

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target uC:       TM4C123GH6PM
// System Clock:    40 MHz

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

#include <stdint.h>
#include "tm4c123gh6pm.h"
#include "mm.h"
#include "psp_msp.h"
#include "gpio.h"
#include "kernel.h"
#include "wait.h"

//-----------------------------------------------------------------------------
// RTOS Defines and Kernel Variables
//-----------------------------------------------------------------------------

extern uint32_t getPsp(void);
extern uint32_t getMsp(void);
extern void setPsp(uint32_t sp);
extern void setAsp(void);
extern void putsUart0(const char *);
extern void putcUart0(char c);
extern void itoa(int num, char *str, int base);
extern void switchToUnpriv(void);

// task states
#define STATE_INVALID           0 // no task
#define STATE_UNRUN             1 // task has never been run
#define STATE_READY             2 // has run, can resume at any time
#define STATE_DELAYED           3 // has run, but now awaiting timer
#define STATE_BLOCKED_SEMAPHORE 4 // has run, but now blocked by semaphore
#define STATE_BLOCKED_MUTEX     5 // has run, but now blocked by mutex
#define STATE_KILLED            6 // task has been killed

struct _tcb tcb[MAX_TASKS];
mutex mutexes[MAX_MUTEXES];
semaphore semaphores[MAX_SEMAPHORES];

// task
uint8_t taskCurrent = 0;          // index of last dispatched task
uint8_t taskCount = 0;            // total number of valid tasks

// control
bool priorityScheduler = true;    // priority (true) or round-robin (false)
bool priorityInheritance = false; // priority inheritance for mutexes
bool preemption = true;          // preemption (true) or cooperative (false)

// tcb
#define NUM_PRIORITIES   8

static uint8_t priorityIndex[NUM_PRIORITIES] = {0};

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

bool initMutex(uint8_t m)
{
    bool ok = (m < MAX_MUTEXES);
    if (ok)
    {
        mutexes[m].lock      = false;
        mutexes[m].lockedBy  = 0;
        mutexes[m].queueSize = 0;
        // clear queue entries
        uint8_t i;
        for (i = 0; i < MAX_MUTEX_QUEUE_SIZE; i++)
            mutexes[m].processQueue[i] = 0;
    }
    return ok;
}

bool initSemaphore(uint8_t semaphore, uint8_t count)
{
    bool ok = (semaphore < MAX_SEMAPHORES);
    if (ok)
    {
        semaphores[semaphore].count     = count;
        semaphores[semaphore].queueSize = 0;
        // clear queue entries
        uint8_t i;
        for (i = 0; i < MAX_SEMAPHORE_QUEUE_SIZE; i++)
            semaphores[semaphore].processQueue[i] = 0;
    }
    return ok;
}

unsigned int stringLen(const char *s)
{
    unsigned int len = 0;
    while (s[len] != '\0')
        len++;
    return len;
}

// REQUIRED: initialize systick for 1ms system timer
void initRtos(void)
{
    uint8_t i;
    // Initialize ALL TCB entries to INVALID
    for (i = 0; i < MAX_TASKS; i++)
    {
        tcb[i].state = STATE_INVALID;
        tcb[i].pid = 0;
        tcb[i].sp = 0;
    }
    taskCount = 0;
    taskCurrent = 0xFF; // No current task

    NVIC_ST_CTRL_R = 0;
    NVIC_ST_RELOAD_R = 40000 - 1;
    NVIC_ST_CURRENT_R = 0;
    NVIC_ST_CTRL_R = NVIC_ST_CTRL_CLK_SRC
                   | NVIC_ST_CTRL_INTEN
                   | NVIC_ST_CTRL_ENABLE;
}

// REQUIRED: Implement prioritization to NUM_PRIORITIES
uint8_t rtosScheduler(void)
{
    //  Round Robin scheduler
    if (!priorityScheduler)
    {
        static uint8_t task = 0xFF;
        uint8_t count = 0;

        while (count < MAX_TASKS)
        {
            task++;
            if (task >= MAX_TASKS)
                task = 0;

            if ((tcb[task].state == STATE_READY || tcb[task].state == STATE_UNRUN) &&
                tcb[task].state != STATE_INVALID &&
                tcb[task].state != STATE_KILLED &&
                tcb[task].pid != 0)
            {
                return task;
            }

            count++;
        }
        putsUart0("No READY tasks (RR)!\n");
        while (1);
    }

    //  Priority based scheduler
    uint8_t bestPriority = NUM_PRIORITIES;
    uint8_t nextTask = 0xFF;
    uint8_t i, j;

    // Find best priority among READY/UNRUN tasks
    for (i = 0; i < MAX_TASKS; i++)
    {
        if ((tcb[i].state == STATE_READY || tcb[i].state == STATE_UNRUN) &&
            tcb[i].state != STATE_INVALID &&
            tcb[i].state != STATE_KILLED &&
            tcb[i].pid != 0)
        {
            if (tcb[i].currentPriority < bestPriority)
                bestPriority = tcb[i].currentPriority;
        }
    }

    if (bestPriority == NUM_PRIORITIES)
    {
        putsUart0("No READY tasks!\n");
        while (1);
    }

    // Round robin within priority, starting after current task
    uint8_t start = priorityIndex[bestPriority];

    for (j = 0; j < MAX_TASKS; j++)
    {
        uint8_t i = (start + j) % MAX_TASKS;

        // Skip current task AND check if ready
        if (i != taskCurrent &&
            (tcb[i].state == STATE_READY || tcb[i].state == STATE_UNRUN) &&
            tcb[i].state != STATE_INVALID &&
            tcb[i].state != STATE_KILLED &&
            tcb[i].pid != 0 &&
            tcb[i].currentPriority == bestPriority)
        {
            nextTask = i;
            priorityIndex[bestPriority] = (i + 1) % MAX_TASKS;
            break;
        }
    }

    // Didn't find a different task, keep current
    if (nextTask == 0xFF)
    {
        if (tcb[taskCurrent].state == STATE_READY || tcb[taskCurrent].state == STATE_UNRUN)
            nextTask = taskCurrent;
        else
        {
            putsUart0("No valid task found!\n");
            while (1);
        }
    }

    return nextTask;
}

// REQUIRED: modify this function to start the operating system
// by calling scheduler, set srd bits, setting PSP, ASP bit, call fn with fn add in R0
// fn set TMPL bit, and PC <= fn
void startRtos(void)
{
    taskCurrent = rtosScheduler();

    tcb[taskCurrent].state = STATE_READY;

    disableMpu();
    applySramAccessMask(tcb[taskCurrent].srd);
    enableMpu();

    uint32_t sp = (uint32_t)tcb[taskCurrent].sp;
    setPsp(sp);

    setAsp();

    _fn fn = (_fn)tcb[taskCurrent].pid;

    switchToUnpriv();

    fn();
}

// REQUIRED:
// add task if room in task list
// store the thread name
// allocate stack space and store top of stack in sp and spInit
// set the srd bits based on the memory allocation
bool createThread(_fn fn, const char name[], uint8_t priority, uint32_t stackBytes)
{
    bool ok = false;
    uint8_t i = 0;
    bool found = false;
    if (taskCount < MAX_TASKS)
    {
        while (!found && (i < MAX_TASKS))
        {
            found = (tcb[i++].pid == fn);
        }
        if (!found)
        {
            //Find first available TCB entry
            i = 0;
            while (tcb[i].state != STATE_INVALID && i < MAX_TASKS)
                i++;
            if (i < MAX_TASKS)
            {
                uint8_t *stackBase = (uint8_t *)malloc_heap(stackBytes, (uint16_t)(i + 1));
                if (stackBase == 0)
                    return false;   // allocation failed
                uint8_t *stackTop = stackBase + stackBytes;
                // 8 byte alignment
                stackTop = (uint8_t *)(((uint32_t)stackTop) & 0xFFFFFFF8);
                // TCB fields
                tcb[i].state           = STATE_UNRUN;
                tcb[i].pid             = fn;
                tcb[i].priority        = priority;
                tcb[i].currentPriority = priority;
                tcb[i].ticks           = 0;
                tcb[i].sp              = (void *)stackTop;   // PSP starts at top of stack
                tcb[i].mutex           = 0xFF;
                tcb[i].semaphore       = 0xFF;
                tcb[i].stackBase       = stackBase;
                tcb[i].stackSize       = stackBytes;

                // Copy the thread name
                uint8_t j = 0;
                while (name[j] != '\0' && j < sizeof(tcb[i].name) - 1)
                {
                    tcb[i].name[j] = name[j];
                    j++;
                }
                tcb[i].name[j] = '\0';

                // MPU SRD mask for this stack region
                tcb[i].srd = createSramAccessMaskForStack((uint32_t)stackBase, stackBytes);

                taskCount++;
                ok = true;
            }
        }
    }
    return ok;
}

// REQUIRED: modify this function to kill a thread
// REQUIRED: free memory, remove any pending semaphore waiting,
//           unlock any mutexes, mark state as killed
//void killThread(_fn fn)
//{
//    Moved to psp_msp.s
//}

// REQUIRED: modify this function to restart a thread, including creating a stack
//void restartThread(_fn fn)
//{
//    Moved to psp_msp.s
//}

// REQUIRED: modify this function to set a thread priority
//void setThreadPriority(_fn fn, uint8_t priority)
//{
//    Moved to psp_msp.s
//}

// REQUIRED: modify this function to yield execution back to scheduler using pendsv
void yield(void)
{
    __asm("    SVC #0");
    __asm("    BX LR");
}

// REQUIRED: modify this function to support 1ms system timer
// execution yielded back to scheduler until time elapses using pendsv
//void sleep(uint32_t tick)
//{
//    Moved to psp_msp.s
//}

// REQUIRED: modify this function to wait a semaphore using pendsv
//void wait(int8_t semaphore)
//{
//    Moved to psp_msp.s
//}

// REQUIRED: modify this function to signal a semaphore is available using pendsv
//void post(int8_t semaphore)
//{
//    Moved to psp_msp.s
//}

// REQUIRED: modify this function to lock a mutex using pendsv
__attribute__((naked)) void lock(int8_t mutex)
{
    __asm("  SVC #2");
    __asm("  BX LR");
}

// REQUIRED: modify this function to unlock a mutex using pendsv
__attribute__((naked)) void unlock(int8_t mutex)
{
    __asm("  SVC #3");
    __asm("  BX LR");
}

// REQUIRED: modify this function to add support for the system timer
// REQUIRED: in preemptive code, add code to request task switch
void sysTickIsr(void)
{
    bool needSwitch = false;

    // CPU time 1 tick = 1 ms of execution time
    if (tcb[taskCurrent].state == STATE_READY)
    {
        // Accumulate runtime for the active task
        tcb[taskCurrent].runTime++;
    }

    // Sleep delays
    int i;
    for (i = 0; i < MAX_TASKS; i++)
    {
        if (tcb[i].state == STATE_DELAYED)
        {
            if (tcb[i].ticks > 0)
            {
                tcb[i].ticks--;
                if (tcb[i].ticks == 0)
                {
                    tcb[i].state = STATE_READY;
                    needSwitch = true;
                }
            }
        }
    }

    // Every 2s compute %CPU per task
    static uint16_t msCounter = 0;
    msCounter++;

    if (msCounter >= 2000)
    {
        msCounter = 0;

        // Sum total time used by all valid tasks
        uint32_t totalTicks = 0;
        int i;
        for (i = 0; i < MAX_TASKS; i++)
        {
            if (tcb[i].state != STATE_INVALID && tcb[i].pid != 0)
                totalTicks += tcb[i].runTime;
        }
        if (totalTicks == 0)
            totalTicks = 1;  // avoid divide-by-zero

        // Compute percentage for each task
        for (i = 0; i < MAX_TASKS; i++)
        {
            if (tcb[i].state != STATE_INVALID && tcb[i].pid != 0)
            {
                // Scale by 100×
                tcb[i].cpuPercent = (tcb[i].runTime * 10000UL) / totalTicks;
                tcb[i].runTime = 0;   // clear for next
            }
        }
    }

    // PendSV only if switch needed
    if (needSwitch && preemption)
        NVIC_INT_CTRL_R |= (1 << 28);
}


// REQUIRED: in coop and preemptive, modify this function to add support for task switching
// REQUIRED: process UNRUN and READY tasks differently
//void pendSvIsr(void)
//{
// left pendsv in faults.c
//}


// REQUIRED: modify this function to add support for the service call
// REQUIRED: in preemptive code, add code to handle synchronization primitives
void svCallIsr(void)
{
    uint32_t *psp;
    uint16_t *pc;
    uint8_t svc_number;

    psp = (uint32_t *)getPsp();

    pc = (uint16_t *)(psp[6] - 2);

    svc_number = ((uint8_t *)pc)[0];

    //Handle SVC
    switch (svc_number)
    {
        int i;
        case 0: // YIELD
            NVIC_INT_CTRL_R |= (1<<28);
            break;

        case 1: // SLEEP
        {
            uint32_t sleep_time = psp[0];

            // Mark task as delayed
            tcb[taskCurrent].ticks = sleep_time;
            tcb[taskCurrent].state = STATE_DELAYED;

            // Trigger context switch when going to sleep
            NVIC_INT_CTRL_R |= (1 << 28);
            break;
        }
        case 2: // LOCK MUTEX
        {
            int8_t m = (int8_t)psp[0];      // R0 = mutex index
            if (m < 0 || m >= MAX_MUTEXES)
                break;                      // ignore invalid index

            mutex *mtx = &mutexes[m];

            if (!mtx->lock)
            {
                // Mutex is free
                mtx->lock = true;
                mtx->lockedBy = taskCurrent;
                tcb[taskCurrent].mutex = m;
            }
            else
            {
                // Already locked block current task
                tcb[taskCurrent].state = STATE_BLOCKED_MUTEX;

                if (mtx->queueSize < MAX_MUTEX_QUEUE_SIZE)
                    mtx->processQueue[mtx->queueSize++] = taskCurrent;

                // Context switch to another ready task
                NVIC_INT_CTRL_R |= (1 << 28);
            }
            break;
        }
        case 3: // UNLOCK MUTEX
        {
            int8_t m = (int8_t)psp[0];
            if (m < 0 || m >= MAX_MUTEXES)
                break;

            mutex *mtx = &mutexes[m];

            // Only the owner may unlock
            if (mtx->lock && mtx->lockedBy == taskCurrent)
            {
                if (mtx->queueSize > 0)
                {
                    // Wake up the first waiting task
                    uint8_t next = mtx->processQueue[0];

                    // Shift queue forward
                    for (i = 1; i < mtx->queueSize; i++)
                        mtx->processQueue[i - 1] = mtx->processQueue[i];
                    mtx->queueSize--;

                    // Transfer ownership
                    mtx->lockedBy = next;
                    tcb[next].mutex = m;
                    tcb[next].state = STATE_READY;
                }
                else
                {
                    // Nobody waiting unlock
                    mtx->lock = false;
                    mtx->lockedBy = 0xFF;
                }

                tcb[taskCurrent].mutex = 0xFF;  // Clear current tasks mutex

                // Give scheduler a chance to run the unblocked task
                NVIC_INT_CTRL_R |= (1 << 28);
            }
            break;
        }
        case 4: // WAIT
        {
            int8_t s = (int8_t)psp[0];
            if (s < 0 || s >= MAX_SEMAPHORES)
                break;

            semaphore *sem = &semaphores[s];

            if (sem->count > 0)
            {
                // Token available
                sem->count--;
                return;
            }

            // No tokens block this task
            tcb[taskCurrent].state     = STATE_BLOCKED_SEMAPHORE;
            tcb[taskCurrent].semaphore = s;

            if (sem->queueSize < MAX_SEMAPHORE_QUEUE_SIZE)
                sem->processQueue[sem->queueSize++] = taskCurrent;

            // Switch to another task
            NVIC_INT_CTRL_R |= (1 << 28);
            break;
        }
        case 5: // POST
        {
            int8_t s = (int8_t)psp[0];
            if (s < 0 || s >= MAX_SEMAPHORES)
                break;

            semaphore *sem = &semaphores[s];

            // Give back a token
            sem->count++;

            if (sem->queueSize > 0)
            {
                // Wake first waiter
                uint8_t next = sem->processQueue[0];

                // Shift queue
                int i;
                for (i = 1; i < sem->queueSize; i++)
                    sem->processQueue[i-1] = sem->processQueue[i];
                sem->queueSize--;

                // Give token to that task
                sem->count--;
                tcb[next].state     = STATE_READY;
                tcb[next].semaphore = 0xFF;

                // context switch when a waiter exists
                NVIC_INT_CTRL_R |= (1 << 28);
            }

            break;
        }
        case 6: // PIDOF
        {
            int i;
            char *target = (char *)psp[0];  //name string pointer in R0
            for (i = 0; i < MAX_TASKS; i++)
            {
                if (tcb[i].pid != 0 && tcb[i].state != STATE_INVALID)
                {
                    char *a = (char*)tcb[i].name;
                    char *b = target;
                    bool match = true;
                    while (*a && *b)
                    {
                        if (*a != *b)
                        {
                            match = false;
                            break;
                        }
                        a++; b++;
                    }
                    if (match && *a == '\0' && *b == '\0')
                    {
                        psp[0] = (uint32_t)tcb[i].pid;
                        return;
                    }
                }
            }
            psp[0] = 0;
            break;
        }
        case 7: // REBOOT
        {
            // Request system reset
            NVIC_APINT_R = 0x05FA0004;

            break;
        }
        case 8: // killThread
        {
            _fn fn = (_fn)psp[0];

            if (fn != 0)
            {
                int i, j;
                int idx = -1;

                // Find the TCB for thread
                for (i = 0; i < MAX_TASKS; i++)
                {
                    if (tcb[i].pid == fn && tcb[i].state != STATE_INVALID)
                    {
                        idx = i;
                        break;
                    }
                }

                if (idx >= 0)
                {
                    // Remove from semaphore wait queue
                    if (tcb[idx].semaphore < MAX_SEMAPHORES)
                    {
                        uint8_t s = tcb[idx].semaphore;
                        semaphore *sem = &semaphores[s];

                        for (j = 0; j < sem->queueSize; )
                        {
                            if (sem->processQueue[j] == idx)
                            {
                                int k;
                                for (k = j + 1; k < sem->queueSize; k++)
                                    sem->processQueue[k-1] = sem->processQueue[k];
                                sem->queueSize--;
                            }
                            else
                            {
                                j++;
                            }
                        }
                        tcb[idx].semaphore = 0xFF;
                    }

                    // Clean up mutexes ownership + wait queues
                    for (i = 0; i < MAX_MUTEXES; i++)
                    {
                        mutex *mtx = &mutexes[i];

                        // If thread owns the mutex
                        if (mtx->lock && mtx->lockedBy == idx)
                        {
                            if (mtx->queueSize > 0)
                            {
                                uint8_t next = mtx->processQueue[0];

                                for (j = 1; j < mtx->queueSize; j++)
                                    mtx->processQueue[j-1] = mtx->processQueue[j];
                                mtx->queueSize--;

                                mtx->lockedBy = next;
                                tcb[next].mutex = i;
                                tcb[next].state = STATE_READY;
                            }
                            else
                            {
                                mtx->lock = false;
                                mtx->lockedBy = 0xFF;
                            }
                        }

                        // If thread is waiting on mutex remove it
                        for (j = 0; j < mtx->queueSize; )
                        {
                            if (mtx->processQueue[j] == idx)
                            {
                                int k;
                                for (k = j + 1; k < mtx->queueSize; k++)
                                    mtx->processQueue[k-1] = mtx->processQueue[k];
                                mtx->queueSize--;
                            }
                            else
                            {
                                j++;
                            }
                        }
                    }
                    tcb[idx].mutex = 0xFF;

                    // Free the threads stack
                    if (idx != taskCurrent && tcb[idx].stackBase != 0)
                    {
                        free_heap(tcb[idx].stackBase, (uint16_t)(idx + 1));
                        tcb[idx].stackBase = 0;
                    }

                    // Mark TCB as killed
                    tcb[idx].state      = STATE_KILLED;
                    tcb[idx].sp         = 0;
                    tcb[idx].ticks      = 0;
                    tcb[idx].runTime    = 0;
                    tcb[idx].cpuPercent = 0;

                    // If killed the running task reschedule
                    if (idx == taskCurrent)
                    {
                        NVIC_INT_CTRL_R |= (1 << 28);
                    }
                }
            }
            break;
        }
        case 9: // restartThread
        {
            _fn fn = (_fn)psp[0];

            if (fn != 0)
            {
                uint32_t savedMask = tcb[taskCurrent].srd;
                applySramAccessMask(0x00000000);

                int i;
                int idx = -1;

                // Find TCB for this function
                for (i = 0; i < MAX_TASKS; i++)
                {
                    if (tcb[i].pid == fn && tcb[i].state != STATE_INVALID)
                    {
                        idx = i;
                        break;
                    }
                }

                if (idx >= 0)
                {
                    // Free old stack
                    if (tcb[idx].stackBase != 0)
                    {
                        free_heap(tcb[idx].stackBase, (uint16_t)(idx + 1));
                        tcb[idx].stackBase = 0;
                    }

                    // Allocate new stack using recorded size
                    uint32_t stackBytes = tcb[idx].stackSize;
                    if (stackBytes == 0)
                        stackBytes = 1024;  // fallback

                    uint8_t *stackBase = (uint8_t *)malloc_heap(stackBytes, (uint16_t)(idx + 1));
                    if (stackBase != 0)
                    {
                        uint8_t *stackTop = stackBase + stackBytes;
                        // 8-byte align
                        stackTop = (uint8_t *)(((uint32_t)stackTop) & 0xFFFFFFF8);

                        tcb[idx].stackBase = stackBase;
                        tcb[idx].sp        = (void *)stackTop;

                        // Rebuild MPU SRD mask for new stack
                        tcb[idx].srd = createSramAccessMaskForStack((uint32_t)stackBase, stackBytes);

                        // Reset runtime fields and state
                        tcb[idx].ticks       = 0;
                        tcb[idx].runTime     = 0;
                        tcb[idx].cpuPercent  = 0;
                        tcb[idx].mutex       = 0xFF;
                        tcb[idx].semaphore   = 0xFF;
                        tcb[idx].state       = STATE_UNRUN;
                    }
                    // else: allocation failed
                }

                // Restore original mask
                applySramAccessMask(savedMask);
            }
            break;
        }
        case 10: // setThreadPriority
        {
            _fn fn = (_fn)psp[0];      // R0 = function pointer
            uint8_t prio = (uint8_t)psp[1];  // R1 = priority

            if (fn != 0)
            {
                uint32_t savedMask = tcb[taskCurrent].srd;
                applySramAccessMask(0x00000000);

                // Clamp to valid range
                if (prio >= NUM_PRIORITIES)
                    prio = NUM_PRIORITIES - 1;

                int i;
                for (i = 0; i < MAX_TASKS; i++)
                {
                    if (tcb[i].pid == fn &&
                        tcb[i].state != STATE_INVALID &&
                        tcb[i].state != STATE_KILLED)
                    {
                        tcb[i].priority        = prio;
                        tcb[i].currentPriority = prio;
                        break;
                    }
                }

                applySramAccessMask(savedMask);

                // Let scheduler consider if needed
                if (priorityScheduler && preemption)
                    NVIC_INT_CTRL_R |= (1 << 28);
            }
            break;
        }
        case 11: // ps()
        {
            uint32_t savedMask = tcb[taskCurrent].srd;
            applySramAccessMask(0x00000000);

            putsUart0("\nNAME            STATE     PRIO  %CPU\n");
            putsUart0("--------------------------------------\n");

            int i, j;
            char str[12];

            for (i = 0; i < MAX_TASKS; i++)
            {
                if (tcb[i].state != STATE_INVALID && tcb[i].pid != 0)
                {
                    // Name
                    putsUart0(tcb[i].name);
                    for (j = stringLen(tcb[i].name); j < 15; j++)
                        putcUart0(' ');

                    // State
                    switch (tcb[i].state)
                    {
                        case STATE_UNRUN:             putsUart0("UNRUN   "); break;
                        case STATE_READY:             putsUart0("READY   "); break;
                        case STATE_DELAYED:           putsUart0("DELAYED "); break;
                        case STATE_BLOCKED_SEMAPHORE: putsUart0("SEM_BLK "); break;
                        case STATE_BLOCKED_MUTEX:     putsUart0("MTX_BLK "); break;
                        case STATE_KILLED:            putsUart0("KILLED  "); break;
                        default:                      putsUart0("INVLD   "); break;
                    }

                    // Priority
                    itoa(tcb[i].priority, str, 10);
                    putsUart0(str);
                    putsUart0("   ");

                    // %CPU
                    uint32_t pct   = tcb[i].cpuPercent;  // scaled ×100
                    uint32_t whole = pct / 100;
                    uint32_t frac  = pct % 100;

                    char wbuf[8], fbuf[4];
                    itoa(whole, wbuf, 10);
                    itoa(frac,  fbuf, 10);

                    putsUart0(wbuf);
                    putcUart0('.');
                    if (frac < 10) putcUart0('0');
                    putsUart0(fbuf);
                    putsUart0("\n");
                }
            }

            applySramAccessMask(savedMask);
            break;
        }
        case 12: // ipcs()
        {
            uint32_t savedMask = tcb[taskCurrent].srd;
            applySramAccessMask(0x00000000);

            putsUart0("\nIPC TYPE  ID   STATE/INFO\n");
            putsUart0("--------------------------------------\n");

            char str[12];
            int i, j;

            // SEMAPHORES
            for (i = 0; i < MAX_SEMAPHORES; i++)
            {
                if (semaphores[i].count == 0 && semaphores[i].queueSize == 0)
                    continue;

                putsUart0("SEM      ");
                itoa(i, str, 10);
                putsUart0(str);
                putsUart0("   count=");
                itoa(semaphores[i].count, str, 10);
                putsUart0(str);
                putsUart0("  waiting=");
                itoa(semaphores[i].queueSize, str, 10);
                putsUart0(str);

                if (semaphores[i].queueSize > 0)
                {
                    putsUart0("  [");
                    for (j = 0; j < semaphores[i].queueSize; j++)
                    {
                        uint8_t pidIdx = semaphores[i].processQueue[j];
                        if (pidIdx < MAX_TASKS)
                        {
                            putsUart0(tcb[pidIdx].name);
                            if (j < semaphores[i].queueSize - 1)
                                putsUart0(", ");
                        }
                    }
                    putsUart0("]");
                }
                putsUart0("\n");
            }

            // MUTEXES
            for (i = 0; i < MAX_MUTEXES; i++)
            {
                if (!mutexes[i].lock && mutexes[i].queueSize == 0)
                    continue;

                putsUart0("MUTEX    ");
                itoa(i, str, 10);
                putsUart0(str);

                putsUart0("   locked=");
                putsUart0(mutexes[i].lock ? "1" : "0");

                putsUart0("  by=");
                if (mutexes[i].lock && mutexes[i].lockedBy < MAX_TASKS)
                    putsUart0(tcb[mutexes[i].lockedBy].name);
                else
                    putsUart0("---");

                putsUart0("  waiting=");
                itoa(mutexes[i].queueSize, str, 10);
                putsUart0(str);

                if (mutexes[i].queueSize > 0)
                {
                    putsUart0("  [");
                    for (j = 0; j < mutexes[i].queueSize; j++)
                    {
                        uint8_t pidIdx = mutexes[i].processQueue[j];
                        if (pidIdx < MAX_TASKS)
                        {
                            putsUart0(tcb[pidIdx].name);
                            if (j < mutexes[i].queueSize - 1)
                                putsUart0(", ");
                        }
                    }
                    putsUart0("]");
                }

                putsUart0("\n");
            }

            applySramAccessMask(savedMask);
            break;
        }
        case 13: // PI
        {
            bool on = (bool)psp[0];  // R0

            uint32_t savedMask = tcb[taskCurrent].srd;
            applySramAccessMask(0x00000000);

            priorityInheritance = on;
            putsUart0(on ? "pi on\n" : "pi off\n");

            applySramAccessMask(savedMask);
            break;
        }

        case 14: // PREEMPT
        {
            bool on = (bool)psp[0];

            uint32_t savedMask = tcb[taskCurrent].srd;
            applySramAccessMask(0x00000000);

            preemption = on;
            putsUart0(on ? "preempt on\n" : "preempt off\n");

            applySramAccessMask(savedMask);
            break;
        }

        case 15: // SCHED
        {
            bool prio_on = (bool)psp[0];

            uint32_t savedMask = tcb[taskCurrent].srd;
            applySramAccessMask(0x00000000);

            priorityScheduler = prio_on;
            putsUart0(prio_on ? "sched prio\n" : "sched rr\n");

            applySramAccessMask(savedMask);
            break;
        }
        default:
            break;
    }
}
