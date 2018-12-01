/*
  GC internal structures and definitions, not part of its API
  Only for including into tests, not other kernel code
*/

#ifndef ELM_KERNEL_GC_INTERNALS
#define ELM_KERNEL_GC_INTERNALS

#include "types.h"
#include <stdbool.h>


#define GC_WASM_PAGE_BYTES 65536
#define GC_BLOCK_BYTES 256
#define GC_BLOCK_WORDS (GC_BLOCK_BYTES/sizeof(size_t))
#define GC_BLOCK_MASK (-(GC_BLOCK_WORDS * sizeof(size_t)))
#define GC_WORD_BITS (sizeof(size_t)*8)
#define GC_DIV_ROUND_UP(num,den) ((num+den-1)/den)


typedef struct {
    size_t* start;
    size_t* end; // end of usable heap area
    size_t** offsets;
    size_t* bitmap;
    size_t* system_end; // end of heap including GC reserved area (bitmap & offsets)
} GcHeap;


typedef struct {
    GcHeap heap;
    size_t* next_alloc;
    ElmValue* roots;
    Custom* stack_map;
    size_t stack_depth;
} GcState;


enum {
    GcStackEmpty,
    GcStackPush,
    GcStackPop,
    GcStackTailCall,
} GcStackCtor;


void bitmap_reset(GcHeap*);
size_t bitmap_live_between(GcHeap* heap, size_t* first, size_t* last);
size_t* forwarding_address(GcHeap* heap, size_t* old_pointer);
void reset_offsets(GcHeap* heap);

size_t child_count(ElmValue* v);
int set_heap_end(GcHeap* heap, size_t* new_break_ptr);
size_t make_bitmask(size_t first_bit, size_t last_bit);
bool mark_words(GcHeap* heap, void* p_void, size_t size);
void mark_trace(GcHeap* heap, ElmValue* v, size_t* ignore_below);
void mark_stack_map(GcState* state, size_t* ignore_below);
void mark(GcState* state, size_t* ignore_below);
void compact(GcState* state, size_t* compact_start);

void collect(GcState*);

#endif
