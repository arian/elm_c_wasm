#include "../kernel/types.h"
#include "../kernel/gc.h"
#include "maybe.h"


Custom Maybe_Nothing;


Closure Maybe_Just;
void* Maybe_Just_eval(void* args[1]) {
    Custom* p = CAN_THROW(GC_malloc(sizeof(Custom) + sizeof(void*)));
    *p = (Custom){
        .header = HEADER_CUSTOM(1),
        .ctor = 1,
    };
    p->values[0] = args[0];
    return p;
}


void Maybe_init() {
    Maybe_Nothing = (Custom){
        .header = HEADER_CUSTOM(0),
        .ctor = 0
    };
    Maybe_Just = F1(Maybe_Just_eval);
}
