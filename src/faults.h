#ifndef FAULTS_H_
#define FAULTS_H_

#include <stdint.h>

//ISR handlers
void BusFaultISR(void);
void UsageFaultISR(void);
void HardFaultISR(void);
void MPUFaultISR(void);
void PendSVISR(void);


#endif /* FAULTS_H_ */
