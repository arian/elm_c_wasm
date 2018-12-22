/*
Immutable FIFO queue
Used in core lib for scheduling proceses and passing messages to them
*/
#include "../kernel/types.h"
#include "../kernel/utils.h"
#include "../kernel/list.h"
#include "../kernel/gc.h"
#include "maybe.h"

enum {
    Fifo_in,
    Fifo_out
};

/*
push : a -> Fifo a -> Fifo a
push value (Fifo in_list outList) =
    Fifo (value :: in_list) outList
*/
Closure Fifo_push;
void* push_eval(void* args[2]) {
    ElmValue* v = args[0];
    Custom* old_fifo = args[1];

    Custom* new_fifo = A1(&Utils_clone, old_fifo);
    Cons* in_list = new_fifo->values[Fifo_in];
    new_fifo->values[Fifo_in] = NEW_CONS(v, in_list);
    return new_fifo;
}


// pop : Fifo a -> ( Maybe a, Fifo a )
Closure Fifo_pop;
void* pop_eval(void* args[1]) {
    Custom* old_fifo = args[0];

    ElmValue* in_list = old_fifo->values[Fifo_in];
    ElmValue* out_list = old_fifo->values[Fifo_out];

    Custom* maybe_popped;
    Custom* new_fifo;

    do {
        if (out_list == &Nil) {
            if (in_list == &Nil) {
                maybe_popped = &Maybe_Nothing;
                new_fifo = old_fifo;
                break;
            }
            in_list = A1(&List_reverse, in_list);
        }
        maybe_popped = A1(&Maybe_Just, in_list->cons.head);
        new_fifo = A1(&Utils_clone, old_fifo);
        new_fifo->values[Fifo_out] = in_list->cons.tail;
    } while (0);

    return NEW_TUPLE2(maybe_popped, new_fifo);
}


Custom* Fifo_empty;

void Fifo_init() {
    Fifo_push = F2(push_eval);
    Fifo_pop = F1(pop_eval);

    Fifo_empty = GC_malloc(sizeof(Custom) + 2*sizeof(void*));
    *Fifo_empty = (Custom){
        .header = HEADER_CUSTOM(2),
        .ctor = 0
    };
    Fifo_empty->values[Fifo_in] = &Nil;
    Fifo_empty->values[Fifo_out] = &Nil;
}
