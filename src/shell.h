// Shell functions
// J Losh

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target uC:       TM4C123GH6PM
// System Clock:    40 MHz

#ifndef SHELL_H_
#define SHELL_H_

#include <stdint.h>
#include <stdbool.h>


#define MAX_CHARS 80
#define MAX_FIELDS 5

typedef struct _USER_DATA
{
    char buffer[MAX_CHARS + 1];
    uint8_t fieldCount;
    uint8_t fieldPosition[MAX_FIELDS];
    char fieldType[MAX_FIELDS]; // 'a' or 'n'
} USER_DATA;


void shell(void);

void getsUart0(USER_DATA* data);
void parseFields(USER_DATA* data);
char* getFieldString(USER_DATA* data, uint8_t fieldNumber);
int32_t getFieldInteger(USER_DATA* data, uint8_t fieldNumber);
bool isCommand(USER_DATA* data, const char strCommand[], uint8_t minArguments);


void itoa(int num, char* str, int base);

void reboot(void);
void ps(void);
void ipcs(void);
void kill(uint32_t pid);
void pkill(const char name[]);
void pi(bool on);
void preempt(bool on);
void sched(bool prio_on);
int pidof(const char name[]);
void run(const char name[]);

#endif // SHELL_H_
