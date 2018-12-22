#ifndef ELM_CORE_FIFO
#define ELM_CORE_FIFO

#include "../kernel/types.h"

Closure Fifo_push;
Closure Fifo_pop;
Custom* Fifo_empty;

void Fifo_init();

#endif
