#include <stdlib.h>
#include "types.h"

// Not a kernel function, will be generated from Elm
void* List_append_eval(void* args[2]) {
    return NULL;
}


Closure List_reverse;
void* List_reverse_eval(void* args[1]) {
    ElmValue* fwd = args[0];
    ElmValue* rev = &Nil;
    while (fwd->header.tag == Tag_Cons) {
        rev = NEW_CONS(fwd->cons.head, rev);
        fwd = fwd->cons.tail;
    }
    return rev;
}


void List_init() {
    // List_reverse = F1(List_reverse_eval);


    List_reverse = (Closure) {
        .header = (Header){
            .tag=Tag_Closure,
            .size=(sizeof(Closure)) // + 0*sizeof(void*))/sizeof(size_t)
        },
        .evaluator = &List_reverse_eval,
        .max_values = 1
    };

}
