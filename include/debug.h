#ifndef DEBUG_H
#define DEBUG_H

#include "uart.h"

#define DEBUG_SCHED 1

#if DEBUG_SCHED
#define dbg_uart(msg) uart_puts(msg)
#else
#define dbg_uart(msg)
#endif

#endif /* DEBUG_H */ 