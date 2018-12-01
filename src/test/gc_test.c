#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "../kernel/types.h"
#include "../kernel/utils.h"
#include "../kernel/basics.h"
#include "../kernel/gc.h"
#include "../kernel/gc-internals.h"
#include "./test.h"

extern GcState gc_state;



bool is_marked(void* p) {
    size_t* pword = (size_t*)p;
    size_t slot = pword - gc_state.heap.start;
    if (slot >> (GC_WORD_BITS-1)) return true; // off heap => not garbage, stop tracing
    size_t word = slot / GC_WORD_BITS;
    size_t bit = slot % GC_WORD_BITS;
    size_t mask = (size_t)1 << bit;

    return (gc_state.heap.bitmap[word] & mask) != 0;
}

void print_value(ElmValue* v) {
    printf("| %12zx |  %c   |  %2x  | ", (size_t)v, is_marked(v) ? 'X' : ' ', v->header.size);
    switch (v->header.tag) {
        case Tag_Int:
            printf("Int %d", v->elm_int.value);
            break;
        case Tag_Float:
            printf("Float %f", v->elm_float.value);
            break;
        case Tag_Char:
            printf("Char 0x%8x", v->elm_char.value);
            break;
        case Tag_String:
            printf("String \"%s\"", v->elm_string.bytes);
            break;
        case Tag_Nil:
            printf("Nil");
            break;
        case Tag_Cons:
            printf("Cons head=%zx tail=%zx", (size_t)v->cons.head, (size_t)v->cons.tail);
            break;
        case Tag_Tuple2:
            printf("Tuple2 a=%zx b=%zx", (size_t)v->tuple2.a, (size_t)v->tuple2.b);
            break;
        case Tag_Tuple3:
            printf("Tuple3 a=%zx b=%zx c=%zx", (size_t)v->tuple3.a, (size_t)v->tuple3.b, (size_t)v->tuple3.c);
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
            printf(" values: ");
            for (size_t i=0; i<custom_params(&v->custom); ++i) {
                printf("%zx ", (size_t)v->custom.values[i]);
            }
            break;
        case Tag_Record:
            printf("Record fieldset=%zx values: ", (size_t)v->record.fieldset);
            for (size_t i=0; i < v->record.fieldset->size; ++i) {
                printf("%zx ", (size_t)v->record.values[i]);
            }
            break;
        case Tag_Closure:
            printf("Closure n_values=%d max_values=%d evaluator=%zx values: ",
                v->closure.n_values, v->closure.max_values, (size_t)v->closure.evaluator
            );
            for (size_t i=0; i < v->closure.n_values; ++i) {
                printf("%zx ", (size_t)v->record.values[i]);
            }
            break;
        case Tag_GcFull:
            printf("GcFull continuation=%zx", (size_t)v->gc_full.continuation);
            break;
    }
    printf("\n");
}

void print_heap(GcState *state) {
    printf("|   Address    | Mark | Size | Value\n");
    printf("| ------------ | ---- | ---- | -----\n");

    size_t* next_value = state->heap.start;
    for (size_t* p = state->heap.start; p < state->next_alloc; p++) {
        if (p == next_value) {
            ElmValue* v = (ElmValue*)p;
            print_value(v);
            if (v->header.size > 0 && v->header.size < 128) {
                next_value += v->header.size;
            } else {
                next_value++;
            }
        } else {
            printf("| %12zx |  %c   |      |\n", (size_t)p, is_marked(p) ? 'X' : ' ');
        }
    }
}


void print_state(GcState* state) {
    printf("start = %zx\n", (size_t)gc_state.heap.start);
    printf("end = %zx\n", (size_t)gc_state.heap.end);
    printf("offsets = %zx\n", (size_t)gc_state.heap.offsets);
    printf("bitmap = %zx\n", (size_t)gc_state.heap.bitmap);
    printf("system_end = %zx\n", (size_t)gc_state.heap.system_end);
    printf("next_alloc = %zx\n", (size_t)gc_state.next_alloc);
    printf("roots = %zx\n", (size_t)gc_state.roots);
    printf("stack_map = %zx\n", (size_t)gc_state.stack_map);
    printf("stack_depth = %zd\n", gc_state.stack_depth);

    // find last non-zero word in the bitmap
    size_t bitmap_size = gc_state.heap.system_end - gc_state.heap.bitmap;
    size_t last_word = bitmap_size;
    while (gc_state.heap.bitmap[--last_word] == 0 && last_word != 0) { }

    printf("Bitmap:\n");
    for (size_t word=0; word <= last_word && word < bitmap_size; word++) {
        #ifdef TARGET_64BIT
            printf("%2zx | %016zx\n", word, gc_state.heap.bitmap[word]);
        #else
            printf("%2zx | %08zx\n", word, gc_state.heap.bitmap[word]);
        #endif
    }

    printf("\n");
}

char alive_or_dead_msg[30];
ElmValue* root_mutable_pointer;

char* gc_stackmap_test() {
    if (verbose) {
        printf("\n");
        printf("Stack map\n");
        printf("---------\n");
    }

    void* live[100];
    void* dead[100];
    size_t nlive=0, ndead=0;


    // GC_init();
    if (verbose) {
        printf("GC initial state:\n");
        print_state(&gc_state);
    }

    // Mock Closures.
    // We never evaluate these during the test so they can be anything
    Closure* mock_effect_callback = &Basics_add; // Basics_add is not really an effect callback. It's just a Closure value.
    Closure* mock_closure = &Basics_mul;

    live[nlive++] = gc_state.heap.start; // stack_empty

    // Call the top-level function (an effect callback or incoming port)
    ElmValue* c1 = (ElmValue*)Utils_clone(mock_effect_callback);
    live[nlive++] = gc_state.next_alloc; // the root Cons cell we're about to allocate
    root_mutable_pointer = c1;
    GC_register_root(&root_mutable_pointer); // Effect manager is keeping this Closure alive by connecting it to the GC root.
    live[nlive++] = c1;
    if (verbose) {
        printf("Kernel module registered root:\n  located at %zx\n  pointing at %zx\n",
            (size_t)&root_mutable_pointer, (size_t)root_mutable_pointer
        );
    }

    void* push1 = GC_stack_push();
    live[nlive++] = push1;

    // The currently-running function allocates some stuff.
    // This function won't have returned by end of test, so it needs these values.
    live[nlive++] = newElmInt(gc_state.stack_depth);
    live[nlive++] = newElmInt(gc_state.stack_depth);
    live[nlive++] = newElmInt(gc_state.stack_depth);

    // Push down to level 2. This will complete. Need its return value
    Closure* c2 = Utils_clone(mock_closure);
    live[nlive++] = c2;
    void* push2 = GC_stack_push();
    live[nlive++] = push2;

    // Temporary values from level 2, not in return value
    dead[ndead++] = newElmInt(gc_state.stack_depth);
    dead[ndead++] = newElmInt(gc_state.stack_depth);

    // 3rd level function call. All dead, since we have the return value of a higher level call.
    void* push3 = GC_stack_push();
    dead[ndead++] = push3;
    dead[ndead++] = newElmInt(gc_state.stack_depth);
    dead[ndead++] = newElmInt(gc_state.stack_depth);
    ElmInt* ret3 = newElmInt(gc_state.stack_depth);
    dead[ndead++] = ret3;
    dead[ndead++] = gc_state.next_alloc; // the pop we're about to allocate
    GC_stack_pop((ElmValue*)ret3, push3);

    // return value from level 2. Keep it to provide to level 1 on replay
    ElmValue* ret2a = (ElmValue*)newElmInt(gc_state.stack_depth);
    ElmValue* ret2b = (ElmValue*)newCons(ret2a, &Nil);
    ElmValue* ret2c = (ElmValue*)newElmInt(gc_state.stack_depth);
    ElmValue* ret2d = (ElmValue*)newCons(ret2c, ret2b);
    live[nlive++] = ret2a;
    live[nlive++] = ret2b;
    live[nlive++] = ret2c;
    live[nlive++] = ret2d;

    // Pop back up to top-level function and allocate a few more things.
    // We actually have a choice whether these are considered alive or dead.
    // Implementation treats them as live for consistency and ease of coding
    live[nlive++] = gc_state.next_alloc; // the pop we're about to allocate
    GC_stack_pop(ret2d, push2);
    live[nlive++] = newElmInt(gc_state.stack_depth);
    live[nlive++] = newElmInt(gc_state.stack_depth);
    live[nlive++] = newElmInt(gc_state.stack_depth);

    // Call a function that makes a tail call
    void* push_into_tailrec = GC_stack_push();
    live[nlive++] = push_into_tailrec;
    dead[ndead++] = newElmInt(gc_state.stack_depth);
    dead[ndead++] = newElmInt(gc_state.stack_depth);

    // Tail call. Has completed when we stop the world => dead
    Closure* ctail1 = GC_malloc(sizeof(Closure) + 2*sizeof(void*));
    ctail1->header = HEADER_CLOSURE(2);
    ctail1->n_values = 2;
    ctail1->max_values = 2;
    ctail1->evaluator = NULL; // just a mock. would be a function pointer in real life
    ctail1->values[0] = dead[ndead-1];
    ctail1->values[1] = dead[ndead-2];
    dead[ndead++] = ctail1;
    dead[ndead++] = gc_state.next_alloc; // tailcall we're about to allocate
    GC_stack_tailcall(ctail1, push_into_tailrec);

    // arguments to the next tail call
    live[nlive++] = newElmInt(gc_state.stack_depth);
    live[nlive++] = newElmInt(gc_state.stack_depth);

    // Tail call. still evaluating this when we stopped the world => keep closure alive
    Closure* ctail2 = GC_malloc(sizeof(Closure) + 2*sizeof(void*));
    ctail2->header = HEADER_CLOSURE(2);
    ctail2->n_values = 2;
    ctail2->max_values = 2;
    ctail1->evaluator = NULL; // just a mock. would be a function pointer in real life
    ctail2->values[0] = live[nlive-1];
    ctail2->values[1] = live[nlive-2];
    live[nlive++] = ctail2;
    live[nlive++] = gc_state.next_alloc; // stack tailcall we're about to allocate
    GC_stack_tailcall(ctail2, push_into_tailrec);

    // allocated just before we stopped the world
    live[nlive++] = newElmInt(gc_state.stack_depth);
    live[nlive++] = newElmInt(gc_state.stack_depth);

    if (verbose) printf("Marking...\n");
    size_t* ignore_below = (size_t*)c1;
    mark(&gc_state, ignore_below);
    if (verbose) printf("Finished marking...\n");

    if (verbose) {
        printf("Final heap state:\n");
        print_heap(&gc_state);
        printf("\n");
        printf("GC final state:\n");
        print_state(&gc_state);
    }

    size_t tested_size = 0;
    while (ndead--) {
        ElmValue* v = (ElmValue*)dead[ndead];
        sprintf(alive_or_dead_msg, "%zx should be dead", (size_t)v);
        mu_assert(alive_or_dead_msg, !is_marked(v));
        tested_size += v->header.size;
    }

    while (nlive--) {
        ElmValue* v = (ElmValue*)live[nlive];
        sprintf(alive_or_dead_msg, "%zx should be live", (size_t)v);
        mu_assert(alive_or_dead_msg, is_marked(v));
        tested_size += v->header.size;
    }

    size_t heap_size = gc_state.next_alloc - gc_state.heap.start;
    mu_assert("Stack map test should account for all allocated values",
        tested_size == heap_size
    );

    if (verbose) printf("\n\nCompacting...\n\n");
    compact(&gc_state, ignore_below);
    if (verbose) printf("Finished compacting\n");


    if (verbose) printf("\n\nMarking...\n\n");
    mark(&gc_state, ignore_below);
    if (verbose) printf("\n\nFinished marking...\n\n");

    if (verbose) {
        print_state(&gc_state);
        print_heap(&gc_state);
    }

    return NULL;
}


char* gc_struct_test() {

    mu_assert("GcHeap should be the size of 5 pointers", sizeof(GcHeap) == 5*sizeof(void*));
    mu_assert("GcState should be the size of 9 pointers", sizeof(GcState) == 9*sizeof(void*));


    return NULL;
}


char bitmap_msg[100];

char* gc_bitmap_test() {
    char str[] = "This is a test string that's an odd number of ints.....";
    GC_init();

    for (size_t i=0; i<10; i++) {
        ElmValue *p1, *p2, *p3, *p4;

        p1 = (ElmValue*)newElmInt(1);
        p2 = (ElmValue*)newElmInt(0);
        p3 = (ElmValue*)newElmString(sizeof(str), str);
        p4 = (ElmValue*)newElmInt(0);

        mark_words(&gc_state.heap, p1, p1->header.size);
        mark_words(&gc_state.heap, p3, p1->header.size);

        mu_assert("p1 should be marked", is_marked(p1));
        mu_assert("p2 should NOT be marked", !is_marked(p2));
        mu_assert("p3 should be marked", is_marked(p3));
        mu_assert("p4 should NOT be marked", !is_marked(p4));
    }

    if (verbose) {
        printf("\n");
        printf("Bitmap test\n");
        printf("-----------\n");
        print_heap(&gc_state);
        print_state(&gc_state);
        printf("\n");
    }

    size_t* bottom_of_heap = gc_state.heap.start;
    size_t* top_of_heap = gc_state.next_alloc;

    size_t* ptr = bottom_of_heap;
    size_t* bitmap = gc_state.heap.bitmap;

    size_t w = 0;
    while (ptr <= top_of_heap) {
        size_t word = bitmap[w];
        for (size_t b=0; b < GC_WORD_BITS; b++) {
            bool bitmap_bit = (word & ((size_t)1 << b)) != 0;
            sprintf(bitmap_msg, "is_marked (%d) should match the bitmap (%d)\naddr = %zx  word = %zd  bit = %zd",
                is_marked(ptr), bitmap_bit, (size_t)ptr, w, b
            );
            mu_assert(bitmap_msg, is_marked(ptr) == bitmap_bit);
            ptr++;
        }
        w++;
    }

    return NULL;
}


char* gc_live_between_test() {
    GC_init();
    GcState* state = &gc_state;
    GcHeap* heap = &state->heap;
    bitmap_reset(heap);

    size_t* first;
    size_t* last;

    heap->bitmap[0] = 0xf0;
    first = heap->start + 4;
    last = heap->start + 8;
    mu_assert("bitmap_live_between with 4 words live",
        bitmap_live_between(heap, first, last) == 4
    );

    first--;
    last++;

    mu_assert("bitmap_live_between with 4 live and 2 junk",
        bitmap_live_between(heap, first, last) == 4
    );

    heap->bitmap[0] = 0xf0;
    heap->bitmap[1] = 0x00;
    heap->bitmap[2] = 0xf0;
    first = heap->start + 2;
    last = heap->start + (2*GC_WORD_BITS) + 10;

    mu_assert("bitmap_live_between across 3 bitmap words",
        bitmap_live_between(heap, first, last) == 8
    );

    last = heap->start + (2*GC_WORD_BITS) + 7;
    mu_assert("bitmap_live_between doesn't count the current word",
        bitmap_live_between(heap, first, last) == 7
    );

    return NULL;
}


char* gc_forwarding_address_test() {
    if (verbose) printf("gc_forwarding_address_test\n");

    GC_init();
    GcState* state = &gc_state;
    GcHeap* heap = &state->heap;
    bitmap_reset(heap);
    reset_offsets(heap);

    size_t* from;
    size_t* expected;
    size_t* actual;

    heap->bitmap[0] = 0xf0;
    heap->offsets[0] = heap->start;
    from = heap->start + 4;
    expected = heap->start;
    mu_assert("forwarding_address at bottom of heap",
        forwarding_address(heap, from) == expected
    );

    heap->bitmap[0] = 0xf0f;
    heap->offsets[0] = heap->start;// + 4;
    from = heap->start + 8;
    expected = heap->start + 4;
    mu_assert("forwarding_address just above bottom of heap",
        forwarding_address(heap, from) == expected
    );

    heap->bitmap[0] = 0xf0f;
    heap->bitmap[0] = 0xff;
    heap->offsets[0] = NULL;
    heap->offsets[1] = heap->start + 8;
    from = heap->start + GC_BLOCK_WORDS;
    expected = heap->offsets[1];
    mu_assert("forwarding_address: 1st value in 2nd block",
        forwarding_address(heap, from) == expected
    );

    heap->bitmap[0] = 0xf0f;
    heap->bitmap[1] = 0xf0f;
    heap->offsets[0] = NULL; // heap->start + 4;
    heap->offsets[1] = heap->start + 8;
    from = heap->start + GC_BLOCK_WORDS + 8;
    expected = heap->start + 12;
    actual = forwarding_address(heap, from);
    if (verbose) {
        print_state(state);
        printf("%zx -> %zx (expected %zx)\n",
            (size_t)from, (size_t)actual, (size_t)expected
        );
    }

    mu_assert("forwarding_address: 2nd value in 2nd block",
        actual == expected
    );


    return NULL;
}


char* gc_test() {
    if (verbose) {
        printf("\n");
        printf("GC\n");
        printf("--\n");
    }

    mu_run_test(gc_struct_test);
    mu_run_test(gc_bitmap_test);
    mu_run_test(gc_live_between_test);
    mu_run_test(gc_forwarding_address_test);
    mu_run_test(gc_stackmap_test);

    return NULL;
}
