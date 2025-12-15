#include "kernel.h"
#include "psp_msp.h"
#include "tm4c123gh6pm.h"
#include "faults.h"
#include "gpio.h"
#include "mm.h"
#include "tasks.h"

extern void putsUart0(const char *);
extern uint8_t rtosScheduler(void);
extern uint8_t taskCurrent;
extern struct _tcb tcb[];
extern void applySramAccessMask(uint32_t srd);

#define BLUE_LED   PORTF,2 // on-board blue LED
#define RED_LED    PORTC,7 // off-board red LED
#define ORANGE_LED PORTD,6 // off-board orange LED
#define YELLOW_LED PORTC,5 // off-board yellow LED
#define GREEN_LED  PORTC,6 // off-board green LED

#define PB1 PORTA,7
#define PB2 PORTA,6
#define PB3 PORTB,4
#define PB4 PORTE,1
#define PB5 PORTE,2
#define PB6 PORTE,3

//Stack frame layout
typedef struct
{
    unsigned int r0;
    unsigned int r1;
    unsigned int r2;
    unsigned int r3;
    unsigned int r12;
    unsigned int lr;
    unsigned int pc;
    unsigned int xpsr;
} StackFrame;

//Integer to ASCII
void itoa(int num, char* str, int base)
{
    int i = 0;
    bool isNegative = false;

    if (num == 0)
    {
        str[i++] = '0';
        str[i] = '\0';
        return;
    }

    if (num < 0 && base == 10)
    {
        isNegative = true;
        num = -num;
    }

    while (num != 0)
    {
        int rem = num % base;
        str[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
        num = num / base;
    }

    if (isNegative)
        str[i++] = '-';

    str[i] = '\0';

    //Reverses the string
    int start = 0, end = i - 1;
    while (start < end)
    {
        char temp = str[start];
        str[start++] = str[end];
        str[end--] = temp;
    }
}

// UART hex printer
void printHex(const char *label, unsigned int val)
{
    putsUart0(label);
    putsUart0("0x");

    char hexDigits[9];  // 8 digits + null terminator
    const char *lookup = "0123456789ABCDEF";

    int i;
    for (i = 0; i < 8; i++)
        hexDigits[i] = '0';

    i = 7;
    while (val != 0 && i >= 0)
    {
        unsigned int digit = val % 16;
        hexDigits[i] = lookup[digit];
        val = val / 16;
        i--;
    }

    hexDigits[8] = '\0';

    putsUart0(hexDigits);
    putsUart0("\n");
}

static void dumpStack(StackFrame *stack)
{
    printHex("PC:   ", stack->pc);
    printHex("xPSR: ", stack->xpsr);
    printHex("LR:   ", stack->lr);
    printHex("R0:   ", stack->r0);
    printHex("R1:   ", stack->r1);
    printHex("R2:   ", stack->r2);
    printHex("R3:   ", stack->r3);
    printHex("R12:  ", stack->r12);
}

void BusFaultISR(void)
{
    setPinValue(ORANGE_LED, 1);  // show Bus fault
    unsigned int pid = (unsigned int)tcb[taskCurrent].pid;

    char str[12];
    putsUart0("Bus fault in process ");
    itoa(pid, str, 10);
    putsUart0(str);
    putsUart0("\n");

    while (1);
}

void UsageFaultISR(void)
{
    setPinValue(YELLOW_LED, 1);  // show usage fault
    unsigned int pid = (unsigned int)tcb[taskCurrent].pid;

    char str[12];
    putsUart0("Usage fault in process ");
    itoa(pid, str, 10);
    putsUart0(str);
    putsUart0("\n");

    while (1);
}

void HardFaultISR(void)
{
    setPinValue(RED_LED, 1);     // show Hard fault
    unsigned int pid = (unsigned int)tcb[taskCurrent].pid;

    uint32_t psp = getPsp();
    uint32_t msp = getMsp();

    putsUart0("Hard fault in process ");
    char str[10];
    itoa(pid, str, 10);
    putsUart0(str);
    putsUart0("\n");

    printHex("MSP:", msp);
    printHex("PSP:", psp);
    printHex("CFSR:", NVIC_FAULT_STAT_R);

    // MMFAR and BFAR
    if (NVIC_FAULT_STAT_R & 0x00000080)  // Bit 7
        printHex("MMFAR:", NVIC_MM_ADDR_R);
    else
        putsUart0("MMFAR: INVALID\n");

    if (NVIC_FAULT_STAT_R & 0x00008000)  // Bit 15
        printHex("BFAR:", NVIC_FAULT_ADDR_R);
    else
        putsUart0("BFAR: INVALID\n");

    StackFrame *stack = (StackFrame *)psp;
    dumpStack(stack);

    while (1);
}

void MPUFaultISR(void)
{
    putsUart0("\n=== MPU FAULT ISR ENTERED ===\n");
    setPinValue(GREEN_LED, 1);
    unsigned int pid = (unsigned int)tcb[taskCurrent].pid;
    StackFrame *stack = (StackFrame *)getPsp();
    uint32_t cfsr = NVIC_FAULT_STAT_R;

    putsUart0("MPU fault in process ");
    char str[12];
    itoa(pid, str, 10);
    putsUart0(str);
    putsUart0("\n");

    printHex("CFSR: ", cfsr);

    // Only read MMFAR if valid
    if (cfsr & (1 << 7))
        printHex("MMFAR:", NVIC_MM_ADDR_R);

    dumpStack(stack);

    //Clear MemManage bits
    NVIC_FAULT_STAT_R = (cfsr & 0xFF);

    //Skip the faulting instruction
    uint16_t *pc16 = (uint16_t *)stack->pc;
    if ((*pc16 & 0xF800) == 0xE800 || (*pc16 & 0xF800) == 0xF000)
        stack->pc += 4;
    else
        stack->pc += 2;

    //Pend PendSV
    NVIC_INT_CTRL_R |= (1 << 28);
}

uint32_t *pendSvC(uint32_t *oldPsp)
{
    tcb[taskCurrent].sp = oldPsp;

    uint8_t next = rtosScheduler();
    if (tcb[next].pid == 0 || tcb[next].state == STATE_INVALID)
        while (1);    // no valid task

    taskCurrent = next;

    applySramAccessMask((uint32_t)tcb[next].srd);

    if (tcb[next].state == STATE_UNRUN)
    {
        tcb[next].state = STATE_READY;
        uint32_t *psp = (uint32_t *)tcb[next].sp;

        // Build hardware frame
        *(--psp) = 0x01000000;                      // xPSR (Thumb)
        *(--psp) = ((uint32_t)tcb[next].pid);       // PC = fn | 1
        *(--psp) = 0xFFFFFFFD;                      // LR = EXC_RETURN (Thread/PSP)
        *(--psp) = 112;                             // R12 (dummy)
        *(--psp) = 103;                             // R3  (dummy)
        *(--psp) = 102;                             // R2  (dummy)
        *(--psp) = 101;                             // R1  (dummy)
        *(--psp) = 100;                             // R0  (dummy or arg)

        // Build software frame R4–R11
        *(--psp) = 0x0000000B;  // R11
        *(--psp) = 0x0000000A;  // R10
        *(--psp) = 0x00000009;  // R9
        *(--psp) = 0x00000008;  // R8
        *(--psp) = 0x00000007;  // R7
        *(--psp) = 0x00000006;  // R6
        *(--psp) = 0x00000005;  // R5
        *(--psp) = 0x00000004;  // R4

        tcb[next].sp = psp;
    }
    return (uint32_t *)tcb[next].sp;
}
