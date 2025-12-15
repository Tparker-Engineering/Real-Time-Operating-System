// Memory manager functions
// J Losh

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target uC:       TM4C123GH6PM
// System Clock:    40 MHz

#ifndef MM_H_
#define MM_H_

#include <stdint.h>
#include <stdbool.h>

//-----------------------------------------------------------------------------
// Memory layout
//-----------------------------------------------------------------------------
#define HEAP_BASE   ((uint8_t *)0x20001000)
#define HEAP_SIZE   (28 * 1024)
#define BLOCK_SIZE  1024
#define MAX_BLOCKS  (HEAP_SIZE / BLOCK_SIZE)

//-----------------------------------------------------------------------------
// Data structures
//-----------------------------------------------------------------------------
typedef struct
{
    bool used;
    uint16_t pid;
    uint16_t length;
} BlockInfo;

//-----------------------------------------------------------------------------
// Function prototypes
//-----------------------------------------------------------------------------

// Heap manager
extern bool free_heap(void *p, uint16_t pid);
extern void *malloc_heap(int size_in_bytes, uint16_t pid);
void   initMemoryManager(void);

// MPU initialization
void initMpu(void);
void enableMpu(void);
void disableMpu(void);
void setupSramAccess(void);
void allowFlashAccess(void);
void allowPeripheralAccess(void);

// Heap access control
uint32_t createNoSramAccessMask(void);
void applySramAccessMask(uint32_t srdMask);
void addSramAccessWindow(uint32_t *srdMask, uint32_t baseAddress, uint32_t size);
uint32_t createSramAccessMaskForStack(uint32_t base, uint32_t size);


#endif

