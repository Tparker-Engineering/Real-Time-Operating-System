# Real-Time-Operating-System

RTOS for the **TI Tiva-C TM4C123GH6PM** featuring a UART command shell, basic IPC (semaphores + mutexes), optional priority scheduling, and per-task SRAM protection using the MPU.

## Target / hardware

- **Board:** EK-TM4C123GXL Evaluation Board  
- **MCU:** TM4C123GH6PM  
- **Clock:** 40 MHz  
- **I/O:** 6 pushbuttons, 5 LEDs, UART0 

## What’s included

### Scheduling + task management
- **1 ms SysTick** timebase
- **Round-robin** scheduler or **priority-based** scheduler (toggle at runtime)
- Cooperative **yield** and optional **preemption** mode

### IPC primitives
- **Semaphores** (counting) and **mutexes**
- **Priority inheritance** can be toggled at runtime (for mutex contention)

### Memory protection + heap
- Uses the **MPU** to control access to flash/peripherals and to restrict each task’s SRAM access using a per-task SRD mask.
- Simple heap-backed stack allocation for each thread.

### UART shell
A simple command-line shell over UART0 for inspecting and controlling the RTOS.

## Built-in demo threads

The default `main()` creates these example threads (names/priorities/stacks are hard-coded):

- `Idle` — required always-ready task
- `Flash4Hz` — blinks an LED periodically
- `OneShot` — waits on a semaphore and pulses an LED
- `LengthyFn` — demonstrates mutex usage around a long critical section
- `ReadKeys` / `Debounce` — button read + debounce pipeline
- `Important` — high-priority example thread
- `Uncoop` — “uncooperative” behavior test thread
- `Errant` — fault / MPU test thread
- `Shell` — UART shell (largest stack)

## Shell commands

Commands currently implemented:

- `reboot`
- `ps` — list tasks + state/priority/%CPU
- `ipcs` — list semaphore/mutex status and waiters
- `kill <pid>`
- `pkill <task_name>`
- `pidof <task_name>`
- `run <task_name>` — restart a thread by name
- `pi on|off` — enable/disable priority inheritance
- `preempt on|off` — enable/disable preemption
- `sched p|r` — priority scheduler (`p`) or round-robin (`r`)


