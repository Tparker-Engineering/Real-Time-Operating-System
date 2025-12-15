#ifndef PSP_MSP_H_
#define PSP_MSP_H_

#include <stdint.h>

uint32_t getPsp(void);
uint32_t getMsp(void);
void     setPsp(uint32_t sp);
void     setAsp(void);
void     switchToPriv(void);
void     switchToUnpriv(void);

void     sleep(uint32_t tick);
void     wait(int8_t semaphore);
void     post(int8_t semaphore);

void     PendSVISR(void);

#endif
