    .def getPsp
    .def getMsp
    .def setPsp
    .def setAsp
    .def switchToUnpriv
    .def PendSVISR
    .def sleep
    .def wait
    .def post
    .def pidof
    .def killThread
    .def restartThread
    .def setThreadPriority
	.ref pendSvC

.thumb
.text

getPsp:
    MRS R0, PSP
    BX LR

getMsp:
    MRS R0, MSP
    BX LR

setPsp:
    MSR PSP, R0
    BX LR

setAsp:
    MRS R0, CONTROL
    ORR R0, R0, #0x2
    MSR CONTROL, R0
    ISB
    BX LR

sleep:
    SVC #1
    BX  LR

wait:
	SVC #4
	BX  LR

post:
	SVC #5
	BX  LR

pidof:
	SVC #6
    BX  LR

killThread:
	SVC #8
	BX  LR

restartThread:
    SVC #9
    BX  LR

setThreadPriority:
    SVC #10
    BX  LR

switchToUnpriv:
    MRS R0, CONTROL
    ORR R0, R0, #1
    MSR CONTROL, R0
    ISB
    BX LR

PendSVISR:
    MRS   r0, psp
    STMDB r0!, {r4-r11}      ; push R4–R11 to thread stack
    MSR   psp, r0

    PUSH  {lr}
    BL    pendSvC
    MSR   psp, r0

    MRS   r0, psp
    LDMIA r0!, {r4-r11}      ; restore R4–R11 of next task
    MSR   psp, r0

    POP   {lr}
    BX    lr


