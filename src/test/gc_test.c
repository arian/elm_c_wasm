#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/types.h"
#include "../kernel/utils.h"
#include "../kernel/basics.h"
#include "../kernel/gc.h"
#include "./test.h"


/*
    stack map
        create by calling apply
        need a sample Elm program
            a few levels of calls
            different numbers of calls
            some allocations
            self tail calls
            mutual tail calls
        Ideally some kind of generator like jasmine marbles
        Maybe one evaluator that parses a string to know what to do
        Configure each function evaluator
            [a,a,a,c(1),a,a,a,c(2),a,c(3),a,a,a,c(self)]
        Configure the point at which it bails
            After the 3rd allocation in function 4

    Wait...
    What errors am I trying to catch?
        Stupid coding mistakes due to C being C
        Don't get carried away
        Design tests bottom-up

    Stack map
        objects are created as expected
        If I create a bunch of stackmap items in a row they are linked
        If I create other objects in-between, they are skipped

    Mark stack map
        Create a stack map
        Create some junk
        Mark it
        Check the right things are marked

    For each test, create a new heap, then fill it with what you want

*/

void print_heap(GcState *state) {

    Header* from = (Header*)state->pages[0].data;
    Header* to = (Header*)state->current_heap;

    printf("|   Address    | Mark | Size | Value\n");
    printf("| ------------ | ---- | ---- | -----\n");
    for (Header* h = from ; h < to ; h += h->size) {
        if (h->size == 0) {
            printf("Zero-size object at %llx = %s\n", (u64)h, hex(h,4));
            printf("Aborting print_heap\n");
            return;
        }
        ElmValue* v = (ElmValue*)h;
        printf("| %llx |  %c   |  %2d  | ", (u64)v, v->header.gc_mark ? 'X' : ' ', v->header.size);
        switch (h->tag) {
            case Tag_Int:
                printf("Int %d", v->elm_int.value);
                break;
            case Tag_Float:
                printf("Float %f", v->elm_float.value);
                break;
            case Tag_Char:
                printf("Char 0x%2x", v->elm_char.value);
                break;
            case Tag_String:
                printf("String \"%s\"", v->elm_string.bytes);
                break;
            case Tag_Nil:
                printf("Nil");
                break;
            case Tag_Cons:
                printf("Cons head=%llx tail=%llx", (u64)v->cons.head, (u64)v->cons.tail);
                break;
            case Tag_Tuple2:
                printf("Tuple2 a=%llx b=%llx", (u64)v->tuple2.a, (u64)v->tuple2.b);
                break;
            case Tag_Tuple3:
                printf("Tuple3 a=%llx b=%llx c=%llx", (u64)v->tuple3.a, (u64)v->tuple3.b, (u64)v->tuple3.c);
                break;
            case Tag_Custom:
                // Assume that Custom type objects are stack map objects
                // Mostly true in GC tests if not in 'real life'
                switch (v->custom.ctor) {
                    case 0: printf("GcStackEmpty"); break;
                    case 1: printf("GcStackPush"); break;
                    case 2: printf("GcStackPop"); break;
                    case 3: printf("GcStackTailCall"); break;
                }
                printf(" values= ");
                for (u32 i=0; i<custom_params(&v->custom); ++i) {
                    printf("%llx ", (u64)v->custom.values[i]);
                }
                break;
            case Tag_Record:
                printf("Record fieldset=%llx values= ", (u64)v->record.fieldset);
                for (u32 i=0; i < v->record.fieldset->size; ++i) {
                    printf("%llx ", (u64)v->record.values[i]);
                }
                break;
            case Tag_Closure:
                printf("Closure n_values=%d max_values=%d evaluator=%llx values= ",
                    v->closure.n_values, v->closure.max_values, (u64)v->closure.evaluator
                );
                for (u32 i=0; i < v->closure.n_values; ++i) {
                    printf("%llx ", (u64)v->record.values[i]);
                }
                break;
            case Tag_GcFull:
                printf("GcFull continuation=%llx", (u64)v->gc_full.continuation);
                break;
        }
        printf("\n");
    }
}


void print_state(GcState* state) {
    printf("system_max_heap=%llx\n", (u64)state->system_max_heap);
    printf("max_heap=%llx\n", (u64)state->max_heap);
    printf("current_heap=%llx\n", (u64)state->current_heap);
    printf("roots=%llx\n", (u64)state->roots);
    printf("stack_map=%llx\n", (u64)state->stack_map);
    printf("stack_depth=%d\n", state->stack_depth);
    printf("pages[0]=%llx\n", (u64)state->pages);
    printf("\n");
}


char* gc_stackmap_test() {
    if (verbose) {
        printf("\n");
        printf("Stack map\n");
        printf("---------\n");
    }

    void* live[100];
    void* dead[100];
    u32 nlive=0, ndead=0;


    GcState* state = GC_init();
    if (verbose) {
        printf("GC initial state:\n");
        print_state(state);
    }

    // Mock Closures.
    // We never evaluate these during the test so they can be anything
    Closure* mock_effect_callback = &Basics_add; // Basics_add is not really an effect callback. It's just a Closure value.
    Closure* mock_closure = &Basics_mul;


    // Call the top-level function (an effect callback or incoming port)
    Closure* c1 = Utils_clone(mock_effect_callback);
    live[nlive++] = state->current_heap; // the root Cons cell we're about to allocate
    GC_register_root(c1); // Effect manager is keeping this Closure alive by connecting it to the GC root.
    live[nlive++] = c1;

    void* push1 = GC_stack_push();
    live[nlive++] = push1;

    // The currently-running function allocates some stuff.
    // This function won't have returned by end of test, so it needs these values.
    live[nlive++] = newElmInt(state->stack_depth);
    live[nlive++] = newElmInt(state->stack_depth);
    live[nlive++] = newElmInt(state->stack_depth);

    // Push down to level 2. This will complete. Need its return value
    Closure* c2 = Utils_clone(mock_closure);
    live[nlive++] = c2;
    void* push2 = GC_stack_push();
    live[nlive++] = push2;

    // Temporary values from level 2, not in return value
    dead[ndead++] = newElmInt(state->stack_depth);
    dead[ndead++] = newElmInt(state->stack_depth);

    // 3rd level function call. All dead, since we have the return value of a higher level call.
    void* push3 = GC_stack_push();
    dead[ndead++] = push3;
    dead[ndead++] = newElmInt(state->stack_depth);
    dead[ndead++] = newElmInt(state->stack_depth);
    ElmInt* ret3 = newElmInt(state->stack_depth);
    dead[ndead++] = ret3;
    dead[ndead++] = state->current_heap; // the pop we're about to allocate
    GC_stack_pop((ElmValue*)ret3, push3);

    // return value from level 2. Keep it to provide to level 1 on replay
    ElmValue* ret2a = (ElmValue*)newElmInt(state->stack_depth);
    ElmValue* ret2b = (ElmValue*)newCons(ret2a, &Nil);
    ElmValue* ret2c = (ElmValue*)newElmInt(state->stack_depth);
    ElmValue* ret2d = (ElmValue*)newCons(ret2c, ret2b);
    live[nlive++] = ret2a;
    live[nlive++] = ret2b;
    live[nlive++] = ret2c;
    live[nlive++] = ret2d;

    // Pop back up to top-level function and allocate a few more things.
    // We actually have a choice whether these are considered alive or dead.
    // Implementation treats them as live for consistency and ease of coding
    live[nlive++] = state->current_heap; // the pop we're about to allocate
    GC_stack_pop(ret2d, push2);
    live[nlive++] = newElmInt(state->stack_depth);
    live[nlive++] = newElmInt(state->stack_depth);
    live[nlive++] = newElmInt(state->stack_depth);

    // Call a function that makes a tail call
    void* push_into_tailrec = GC_stack_push();
    live[nlive++] = push_into_tailrec;
    dead[ndead++] = newElmInt(state->stack_depth);
    dead[ndead++] = newElmInt(state->stack_depth);

    // Tail call. Has completed when we stop the world => dead
    Closure* ctail1 = GC_allocate(sizeof(Closure) + 2*sizeof(void*));
    ctail1->header = HEADER_CLOSURE(2);
    ctail1->n_values = 2;
    ctail1->max_values = 2;
    ctail1->evaluator = NULL; // just a mock. would be a function pointer in real life
    ctail1->values[0] = dead[ndead-1];
    ctail1->values[1] = dead[ndead-2];
    dead[ndead++] = ctail1;
    dead[ndead++] = state->current_heap; // tailcall we're about to allocate
    GC_stack_tailcall(ctail1, push_into_tailrec);

    // arguments to the next tail call
    live[nlive++] = newElmInt(state->stack_depth);
    live[nlive++] = newElmInt(state->stack_depth);

    // Tail call. still evaluating this when we stopped the world => keep closure alive
    Closure* ctail2 = GC_allocate(sizeof(Closure) + 2*sizeof(void*));
    ctail2->header = HEADER_CLOSURE(2);
    ctail2->n_values = 2;
    ctail2->max_values = 2;
    ctail1->evaluator = NULL; // just a mock. would be a function pointer in real life
    ctail2->values[0] = live[nlive-1];
    ctail2->values[1] = live[nlive-2];
    live[nlive++] = ctail2;
    live[nlive++] = state->current_heap; // stack tailcall we're about to allocate
    GC_stack_tailcall(ctail2, push_into_tailrec);

    // allocated just before we stopped the world
    live[nlive++] = newElmInt(state->stack_depth);
    live[nlive++] = newElmInt(state->stack_depth);


    if (verbose) {
        printf("GC final state:\n");
        print_state(state);
    }

    ElmValue* ignore_below = (ElmValue*)c1;
    mark(ignore_below);

    if (verbose) {
        printf("Final heap state:\n");
        print_heap(state);
    }

    u32 tested_size = 0;
    char* msg;
    while (ndead--) {
        ElmValue* v = (ElmValue*)dead[ndead];
        msg = malloc(30);
        sprintf(msg, "%llx should be dead", (u64)v);
        mu_assert(msg, v->header.gc_mark == 0);
        tested_size += v->header.size;
    }

    while (nlive--) {
        ElmValue* v = (ElmValue*)live[nlive];
        msg = malloc(30);
        sprintf(msg, "%llx should be live", (u64)v);
        mu_assert(msg, v->header.gc_mark == 1);
        tested_size += v->header.size;
    }

    u32 heap_size = state->current_heap - state->pages[0].data;
    mu_assert("Stack map test should account for all allocated values",
        tested_size == heap_size
    );

    return NULL;
}



char* gc_mark_test() {


    return NULL;
}


char* gc_test() {
    if (verbose) {
        printf("\n");
        printf("GC\n");
        printf("--\n");
    }

    mu_run_test(gc_stackmap_test);
    mu_run_test(gc_mark_test);
    return NULL;
}
