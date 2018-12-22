#ifndef ELM_CORE_MAYBE
#define ELM_CORE_MAYBE

#include "../kernel/types.h"

void* Maybe_Just_eval(void* args[]);
Closure Maybe_Just;
Custom Maybe_Nothing;
void Maybe_init();

#endif
