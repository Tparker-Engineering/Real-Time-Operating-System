// Memory manager functions
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

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

extern uint8_t taskCurrent;
static BlockInfo blockTable[MAX_BLOCKS];

void *malloc_heap(int size_in_bytes, uint16_t pid)
{
    int i, j;
    if (size_in_bytes <= 0 || pid == 0)
        return 0;

    uint32_t blocksNeeded = (size_in_bytes + BLOCK_SIZE - 1) / BLOCK_SIZE;

    for (i = 0; i < MAX_BLOCKS; i++)
    {
        if (i + blocksNeeded > MAX_BLOCKS)
            continue;

        bool free = true;
        for (j = 0; j < blocksNeeded; j++)
        {
            if (blockTable[i + j].used)
            {
                free = false;
                i += j; //skip past occupied blocks
                break;
            }
        }

        if (!free)
            continue;

        for (j = 0; j < blocksNeeded; j++)
        {
            blockTable[i + j].used = true;
            blockTable[i + j].pid = pid;
            blockTable[i + j].length = 0;
        }

        blockTable[i].length = blocksNeeded;
        return (void *)(HEAP_BASE + (i * BLOCK_SIZE));
    }

    printHex("malloc return:", (uint32_t)(HEAP_BASE + i*BLOCK_SIZE));

    return 0;
}

bool free_heap(void *p, uint16_t pid)
{
    uint32_t i;
    if (p == 0 || pid == 0)
        return false;

    uintptr_t addr = (uintptr_t)p;
    uintptr_t base = (uintptr_t)HEAP_BASE;

    //Must be inside heap and be block aligned
    if (addr < base || addr >= base + HEAP_SIZE)
        return false;
    if ((addr - base) % BLOCK_SIZE != 0)
        return false;

    uint32_t index = (addr - base) / BLOCK_SIZE;
    BlockInfo *blk = &blockTable[index];

    //Must be a valid head block
    if (!blk->used || blk->pid != pid || blk->length == 0)
        return false;

    uint32_t count = blk->length;
    if (index + count > MAX_BLOCKS)
        return false;

    for (i = 0; i < count; i++)
    {
        blockTable[index + i].used = false;
        blockTable[index + i].pid = 0;
        blockTable[index + i].length = 0;
    }

    return true;
}

void initMemoryManager(void)
{
    uint32_t i;
    for (i = 0; i < MAX_BLOCKS; i++)
    {
        blockTable[i].used = false;
        blockTable[i].pid = 0;
        blockTable[i].length = 0;
    }
}

#define REGION_ENABLE (1U)
#define XN_ENABLE (1U << 28)

void enableMpu(void)
{
    NVIC_MPU_CTRL_R = NVIC_MPU_CTRL_ENABLE | NVIC_MPU_CTRL_PRIVDEFEN;
}

void disableMpu(void)
{
    NVIC_MPU_CTRL_R = 0;
}

void setupSramAccess(void)
{
    // region 0 0x20000000 - 0x20001FFF
    NVIC_MPU_NUMBER_R = 0;
    NVIC_MPU_BASE_R = 0x20000000;
    NVIC_MPU_ATTR_R = REGION_ENABLE
                    | (12 << 1)
                    | (0b11 << 24)
                    | (0xFF << 8);

    // region 1 0x20002000 - 0x20003FFF
    NVIC_MPU_NUMBER_R = 1;
    NVIC_MPU_BASE_R = 0x20002000;
    NVIC_MPU_ATTR_R = REGION_ENABLE
                    | (12 << 1)
                    | (0b11 << 24)
                    | (0xFF << 8);

    // region 2 0x20004000 - 0x20005FFF
    NVIC_MPU_NUMBER_R = 2;
    NVIC_MPU_BASE_R = 0x20004000;
    NVIC_MPU_ATTR_R = REGION_ENABLE
                    | (12 << 1)
                    | (0b11 << 24)
                    | (0xFF << 8);

    // region 3 0x20006000 - 0x20007FFF
    NVIC_MPU_NUMBER_R = 3;
    NVIC_MPU_BASE_R = 0x20006000;
    NVIC_MPU_ATTR_R = REGION_ENABLE
                    | (12 << 1)
                    | (0b11 << 24)
                    | (0xFF << 8);
}

// Flash and Peripheral Regions
void allowFlashAccess(void)
{
    NVIC_MPU_NUMBER_R = 5;
    NVIC_MPU_BASE_R = 0x00000000;
    NVIC_MPU_ATTR_R = REGION_ENABLE
                    | (17 << 1)
                    | (0b11 << 24);
}

void allowPeripheralAccess(void)
{
    NVIC_MPU_NUMBER_R = 6;
    NVIC_MPU_BASE_R = 0x40000000;
    NVIC_MPU_ATTR_R = REGION_ENABLE
                    | (28 << 1)
                    | (0b11 << 24)
                    | XN_ENABLE;
}

// Initialization wrapper
void initMemoryProtection(void)
{
    setupSramAccess();
    allowFlashAccess();
    allowPeripheralAccess();
    enableMpu();
}

// Custom helpers for SRD masks
uint32_t createNoSramAccessMask(void)
{
    return 0xFFFFFFFF;
}

void applySramAccessMask(uint32_t srdBitMask)
{
    int region;
    for (region = 0; region < 4; region++)
    {
        NVIC_MPU_NUMBER_R = region;
        volatile uint32_t attr = NVIC_MPU_ATTR_R;  // read to latch
        attr &= ~(0xFF << 8);                      // clear SRD bits
        attr |= ((srdBitMask >> (region * 8)) & 0xFF) << 8;
        NVIC_MPU_ATTR_R = attr;                    // write back
    }
}

void addSramAccessWindow(uint32_t *srdMask, uint32_t baseAddress, uint32_t size)
{
    uint32_t offset = baseAddress - 0x20000000;
    uint32_t index = offset / 1024;

    while (size > 0 && index < 32)
    {
        *srdMask &= ~(1U << index);
        index++;
        size -= 1024;
    }
}

uint32_t createSramAccessMaskForStack(uint32_t base, uint32_t size)
{
    uint32_t mask = createNoSramAccessMask();
    addSramAccessWindow(&mask, base, size + 1024);
    return mask;
}


// REQUIRED: initialize MPU here
void initMpu(void)
{
    disableMpu();
    setupSramAccess();
    allowFlashAccess();
    allowPeripheralAccess();
    applySramAccessMask(0x00000000);
    enableMpu();
}

