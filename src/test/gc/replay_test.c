#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "../../kernel/gc-internals.h"
#include "../../kernel/gc.h"
#include "../../kernel/types.h"
#include "../../kernel/utils.h"
#include "../gc_test.h"
#include "../test.h"
#include "./print-heap.h"

extern GcState gc_state;

Tag mock_func_tags[10];
i32 mock_func_count;

const u32 INT_OFFSET = 256000;

void* eval_mock_func(void* args[2]) {
  void* last_alloc = NULL;
  for (mock_func_count = 0;; mock_func_count++) {
    Tag current_tag = mock_func_tags[mock_func_count];
    switch (current_tag) {
      case Tag_Int:
        last_alloc = NEW_ELM_INT(INT_OFFSET + mock_func_count);
        // printf("allocated at %p, count=%d\n", last_alloc, mock_func_count);
        break;
      case Tag_GcException:
        // printf("throwing exception, count=%d\n", mock_func_count);
        return pGcFull;
      case Tag_GcStackPop:
        // printf("returning pointer %p, count=%d\n", last_alloc, mock_func_count);
        return last_alloc;
      default:
        fprintf(stderr, "Unhandled tag in eval_mock_func %x\n", current_tag);
    }
  }
}

const Closure mock_func = {
    .header = HEADER_CLOSURE(0),
    .evaluator = &eval_mock_func,
    .max_values = 2,
};

int next_value_matches(size_t** p, const void* v) {
  Header* h = (Header*)v;
  bool areEqual = memcmp(*p, v, h->size * SIZE_UNIT) == 0;
  *p += h->size;
  return areEqual;
}

char assert_heap_message[1024];

char* assert_heap_values(const char* description, const void* values[]) {
  size_t* heap_value = gc_state.heap.start;
  size_t i = 0;
  size_t* bad_addr = NULL;
  size_t expected_value;

  while (true) {
    ElmValue* v = (ElmValue*)values[i];
    if (v == NULL) break;

    size_t children;
    switch (v->header.tag) {
      case Tag_GcStackEmpty:
      case Tag_GcStackPush:
      case Tag_GcStackPop:
      case Tag_GcStackTailCall:
        children = 3;
        break;
      default:
        children = child_count(v);
        break;
    }

    size_t* p = (size_t*)values[i];
    size_t* v_end = p + v->header.size;
    size_t* first_child = v_end - children;
    size_t* heap_word = heap_value;

    for (; p < first_child; heap_word++, p++) {
      expected_value = *p;
      if (*heap_word != expected_value) {
        bad_addr = heap_word;
        if (verbose) {
          printf("\n");
          printf("Mismatch at word %zd (tag %x with %zu children)\n",
              heap_word - heap_value,
              v->header.tag,
              children);
          print_value(v);
          Header myheader = HEADER_GC_STACK_POP;
          print_value((ElmValue*)&myheader);
          printf("\n");
        }
        break;
      }
    }
    if (bad_addr) break;

    for (; p < v_end; heap_word++, p++) {
      size_t relative_offset = *p;
      size_t heap_child_addr = *heap_word;
      size_t heap_parent_addr = (size_t)heap_value;
      expected_value = heap_parent_addr + relative_offset;
      if (heap_child_addr == 0 && relative_offset == 0) {
        continue;
      }
      if (heap_child_addr != expected_value) {
        if (verbose)
          printf("Mismatch in child pointer (tag %x with %zu children)\n",
              v->header.tag,
              children);
        bad_addr = heap_word;
        break;
      }
    }
    if (bad_addr) break;

    heap_value = heap_word;
    i++;
  }

  if (bad_addr) {
    print_heap();
    sprintf(assert_heap_message,
        "%s\nExpected %p to be " ZERO_PAD_HEX " but found " ZERO_PAD_HEX "\n",
        description,
        bad_addr,
        expected_value,
        *bad_addr);
    return assert_heap_message;
  } else {
    if (verbose) printf("PASS: %s\n", description);
    return NULL;
  }
}

char* test_gc_replay_finished() {
  // SETUP
  gc_test_reset();
  memcpy(mock_func_tags,
      (Tag[]){
          Tag_Int,
          Tag_Int,
          Tag_Int,
          Tag_GcStackPop,
      },
      sizeof(Tag[4]));

  // RUN
  void* result1 = Utils_apply(&mock_func, 2, (void* []){NULL, NULL});
  ElmInt expected_result = {
      .header = HEADER_INT,
      .value = 3,
  };
  mu_assert("Return value",
      memcmp(result1, &expected_result, expected_result.header.size) == 0);

  // HEAP BEFORE GC
  char* heap_err_before_gc = assert_heap_values("Replay finished call, heap before GC",
      (void* []){
          &(GcStackMap){
              .header = HEADER_GC_STACK_EMPTY,
          },
          &(GcStackMap){
              .header = HEADER_GC_STACK_PUSH,
              .older = (void*)(-sizeof(GcStackMap)),
          },
          &(ElmInt){
              .header = HEADER_INT,
              .value = INT_OFFSET,
          },
          &(ElmInt){
              .header = HEADER_INT,
              .value = INT_OFFSET + 1,
          },
          &(ElmInt){
              .header = HEADER_INT,
              .value = INT_OFFSET + 2,
          },
          &(GcStackMap){
              .header = HEADER_GC_STACK_POP,
              .older = (void*)(-(3 * sizeof(ElmInt) + sizeof(GcStackMap))),
              .replay = (void*)(-sizeof(ElmInt)),
          },
          NULL,
      });
  if (heap_err_before_gc) return heap_err_before_gc;

  // GC + REPLAY
  GC_collect_full();
  GC_start_replay();
  void* result2 = Utils_apply(&mock_func, 2, (void* []){NULL, NULL});
  mu_assert("Replay return value",
      memcmp(result2, &expected_result, expected_result.header.size) == 0);

  // HEAP AFTER GC
  const void* heap_after_spec[] = {
      &(GcStackMap){
          .header = HEADER_GC_STACK_EMPTY,
          .newer = (void*)(sizeof(GcStackMap)),
      },
      &(GcStackMap){
          .header = HEADER_GC_STACK_PUSH,
          .older = (void*)(-sizeof(GcStackMap)),
          .newer = (void*)(sizeof(GcStackMap) + sizeof(ElmInt)),
      },
      &(ElmInt){
          .header = HEADER_INT,
          .value = INT_OFFSET + 2,
      },
      &(GcStackMap){
          .header = HEADER_GC_STACK_POP,
          .older = (void*)(-sizeof(GcStackMap) - sizeof(ElmInt)),
          .replay = (void*)(-sizeof(ElmInt)),
          .newer = (void*)(sizeof(GcStackMap)),
      },
      NULL,
  };
  char* heap_err_after_gc =
      assert_heap_values("Replay finished call, heap after GC", heap_after_spec);
  mu_assert(heap_err_after_gc, heap_err_after_gc == NULL);

  mu_assert("GC State replay_ptr", gc_state.replay_ptr == NULL);
  mu_assert("GC State stack_depth", gc_state.stack_depth == 0);
  mu_assert("GC State stackmap",
      (void*)gc_state.stack_map - (void*)gc_state.heap.start ==
          2 * sizeof(GcStackMap) + sizeof(ElmInt));

  return NULL;
}

char* replay_scenario_tests() {
  mu_run_test(test_gc_replay_finished);
  // mu_run_test(test_gc_replay_finished_original);
  return NULL;
}
