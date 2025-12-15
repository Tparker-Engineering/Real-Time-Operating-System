// Shell functions
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
#include <stdlib.h>
#include "tm4c123gh6pm.h"
#include "shell.h"

// REQUIRED: Add header files here for your strings functions, ...
#include "uart0.h"
#include "gpio.h"
#include "clock.h"
#include "kernel.h"
#include "mm.h"
#include "wait.h"
#include "faults.h"
#include "psp_msp.h"


#define MAX_CHARS 80
#define MAX_FIELDS 5

// UART Input / Parsing
void getsUart0(USER_DATA* data)
{
    uint8_t count = 0;
    char c;

    while (true)
    {
        yield();
        if (kbhitUart0())
        {
            c = getcUart0();

            if ((c == 8 || c == 127) && count > 0)
            {
                count--;
            }
            else if (c == 13)
            {
                data->buffer[count] = '\0';
                return;
            }
            else if (c >= 32 && count < MAX_CHARS)
            {
                data->buffer[count++] = c;
            }
        }
    }
}

void parseFields(USER_DATA* data)
{
    uint8_t i = 0, fieldIndex = 0;
    char prev = '\0';

    for (i = 0; i < MAX_CHARS && data->buffer[i] != '\0'; i++)
    {
        char c = data->buffer[i];

        if (((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z')))
        {
            if (!(((prev >= 'A') && (prev <= 'Z')) || ((prev >= 'a') && (prev <= 'z')) ||
                  ((prev >= '0') && (prev <= '9')) || prev == '-' || prev == '.'))
            {
                data->fieldPosition[fieldIndex] = i;
                data->fieldType[fieldIndex] = 'a';
                fieldIndex++;
                if (fieldIndex == MAX_FIELDS) break;
            }
        }
        else if (((c >= '0') && (c <= '9')) || c == '-' || c == '.')
        {
            if (!(((prev >= 'A') && (prev <= 'Z')) || ((prev >= 'a') && (prev <= 'z')) ||
                  ((prev >= '0') && (prev <= '9')) || prev == '-' || prev == '.'))
            {
                data->fieldPosition[fieldIndex] = i;
                data->fieldType[fieldIndex] = 'n';
                fieldIndex++;
                if (fieldIndex == MAX_FIELDS) break;
            }
        }
        else
        {
            data->buffer[i] = '\0';
        }

        prev = c;
    }

    data->fieldCount = fieldIndex;
}

char* getFieldString(USER_DATA* data, uint8_t fieldNumber)
{
    if (fieldNumber < data->fieldCount)
        return &(data->buffer[data->fieldPosition[fieldNumber]]);
    return 0;
}

int32_t getFieldInteger(USER_DATA* data, uint8_t fieldNumber)
{
    if (fieldNumber < data->fieldCount && data->fieldType[fieldNumber] == 'n')
        return atoi(&(data->buffer[data->fieldPosition[fieldNumber]]));
    return 0;
}

bool isCommand(USER_DATA* data, const char strCommand[], uint8_t minArguments)
{
    if (data->fieldCount == 0 || data->fieldType[0] != 'a')
        return false;

    char* cmd = &data->buffer[data->fieldPosition[0]];
    uint8_t i = 0;
    while (strCommand[i] != '\0' && cmd[i] != '\0')
    {
        char a = strCommand[i];
        char b = cmd[i];

        if (a >= 'a' && a <= 'z') a -= 32;
        if (b >= 'a' && b <= 'z') b -= 32;
        if (a != b) return false;

        i++;
    }

    return (strCommand[i] == '\0' && cmd[i] == '\0' && (data->fieldCount - 1) >= minArguments);
}

// Shell Command Handlers
void reboot(void)
{
    putsUart0("reboot\n");
    __asm(" SVC #7");
}

__attribute__((naked)) void ps(void)
{
    __asm(" SVC #11");
    __asm(" BX  LR");
}

__attribute__((naked)) void ipcs(void)
{
    __asm(" SVC #12");
    __asm(" BX  LR");
}

void kill(uint32_t pid)
{
    if (pid == 0)
    {
        putsUart0("invalid pid\n");
        return;
    }

    killThread((_fn)pid);
}

void pkill(const char name[])
{
    int pid = pidof(name);
    if (pid == 0)
    {
        putsUart0("no such task: ");
        putsUart0((char*)name);
        putsUart0("\n");
        return;
    }

    killThread((_fn)pid);
}

__attribute__((naked)) void pi(bool on)
{
    (void)on;
    __asm(" SVC #13");
    __asm(" BX  LR");
}

__attribute__((naked)) void preempt(bool on)
{
    (void)on;
    __asm(" SVC #14");
    __asm(" BX  LR");
}

__attribute__((naked)) void sched(bool prio_on)
{
    (void)prio_on;
    __asm(" SVC #15");
    __asm(" BX  LR");
}

void run(const char name[])
{
    int pid = pidof(name);
    if (pid == 0)
    {
        putsUart0("no such task: ");
        putsUart0((char*)name);
        putsUart0("\n");
        return;
    }

    restartThread((_fn)pid);
}

// REQUIRED: add processing for the shell commands through the UART here
void shell(void)
{
    USER_DATA data;
    while (true)
    {
        putsUart0("\n> ");
        getsUart0(&data);
        parseFields(&data);

        if (isCommand(&data, "reboot", 0))
            reboot();
        else if (isCommand(&data, "ps", 0))
            ps();
        else if (isCommand(&data, "ipcs", 0))
            ipcs();
        else if (isCommand(&data, "kill", 1))
            kill(getFieldInteger(&data, 1));
        else if (isCommand(&data, "pkill", 1))
            pkill(getFieldString(&data, 1));
        else if (isCommand(&data, "pi", 1))
        {
            char* arg = getFieldString(&data, 1);
            if (arg[0] == 'O' || arg[0] == 'o')
            {
                if (arg[1] == 'N' || arg[1] == 'n') pi(true);
                else if (arg[1] == 'F' || arg[1] == 'f') pi(false);
            }
        }
        else if (isCommand(&data, "preempt", 1))
        {
            char* arg = getFieldString(&data, 1);
            if (arg[0] == 'O' || arg[0] == 'o')
            {
                if (arg[1] == 'N' || arg[1] == 'n') preempt(true);
                else if (arg[1] == 'F' || arg[1] == 'f') preempt(false);
            }
        }
        else if (isCommand(&data, "sched", 1))
        {
            char* arg = getFieldString(&data, 1);
            if ((arg[0] == 'P' || arg[0] == 'p')) sched(true);
            else if ((arg[0] == 'R' || arg[0] == 'r')) sched(false);
        }
        else if (isCommand(&data, "pidof", 1))
        {
            const char *name = getFieldString(&data, 1);
            int pid = pidof(name);

            putsUart0("PID of ");
            putsUart0((char*)name);
            putsUart0(": ");

            char buf[12];
            itoa(pid, buf, 10);
            putsUart0(buf);
            putsUart0("\n");
        }
        else if (isCommand(&data, "run", 1))
        {
            run(getFieldString(&data, 1));
        }
    }
}
