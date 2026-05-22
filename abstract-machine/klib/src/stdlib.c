#include <am.h>
#include <klib.h>
#include <klib-macros.h>
#include <stdint.h>

#if !defined(__ISA_NATIVE__) || defined(__NATIVE_USE_KLIB__)
static unsigned long int next = 1;
typedef struct malloc_block {
  size_t size;
  int free;
  struct malloc_block *next;
} malloc_block_t;

#define MALLOC_ALIGN 8
#define MALLOC_MIN_SPLIT (sizeof(malloc_block_t) + MALLOC_ALIGN)

static malloc_block_t *malloc_head = NULL;

static size_t align_up_size(size_t size) {
  return (size + MALLOC_ALIGN - 1) & ~(MALLOC_ALIGN - 1);
}

static uintptr_t align_up_ptr(uintptr_t ptr) {
  return (ptr + MALLOC_ALIGN - 1) & ~((uintptr_t)MALLOC_ALIGN - 1);
}

static void malloc_init(void) {
  uintptr_t start;
  uintptr_t end;

  if (malloc_head != NULL) return;

  start = align_up_ptr((uintptr_t)heap.start);
  end = (uintptr_t)heap.end;
  if (start >= end || end - start <= sizeof(malloc_block_t)) return;

  malloc_head = (malloc_block_t *)start;
  malloc_head->size = end - start - sizeof(malloc_block_t);
  malloc_head->free = 1;
  malloc_head->next = NULL;
}

static void split_block(malloc_block_t *block, size_t size) {
  malloc_block_t *new_block;

  if (block->size < size + MALLOC_MIN_SPLIT) return;

  new_block = (malloc_block_t *)((char *)(block + 1) + size);
  new_block->size = block->size - size - sizeof(malloc_block_t);
  new_block->free = 1;
  new_block->next = block->next;

  block->size = size;
  block->next = new_block;
}

static void coalesce_blocks(void) {
  malloc_block_t *block = malloc_head;

  while (block != NULL && block->next != NULL) {
    char *block_end = (char *)(block + 1) + block->size;
    if (block->free && block->next->free && block_end == (char *)block->next) {
      block->size += sizeof(malloc_block_t) + block->next->size;
      block->next = block->next->next;
    } else {
      block = block->next;
    }
  }
}

int rand(void) {
  // RAND_MAX assumed to be 32767
  next = next * 1103515245 + 12345;
  return (unsigned int)(next/65536) % 32768;
}

void srand(unsigned int seed) {
  next = seed;
}

int abs(int x) {
  return (x < 0 ? -x : x);
}

int atoi(const char* nptr) {
  int x = 0;
  while (*nptr == ' ') { nptr ++; }
  while (*nptr >= '0' && *nptr <= '9') {
    x = x * 10 + *nptr - '0';
    nptr ++;
  }
  return x;
}

void *malloc(size_t size) {
  // On native, malloc() will be called during initializaion of C runtime.
  // Therefore do not call panic() here, else it will yield a dead recursion:
  //   panic() -> putchar() -> (glibc) -> malloc() -> panic()
#if !(defined(__ISA_NATIVE__) && defined(__NATIVE_USE_KLIB__))
  static char zero_;
  malloc_block_t *block;

  if (size == 0) return (void *)&zero_;

  malloc_init();
  if (malloc_head == NULL) return NULL;

  size = align_up_size(size);
  for (block = malloc_head; block != NULL; block = block->next) {
    if (block->free && block->size >= size) {
      split_block(block, size);
      block->free = 0;
      return (void *)(block + 1);
    }
  }
  return NULL;
#endif
  return NULL;
}

void free(void *ptr) {
#if !(defined(__ISA_NATIVE__) && defined(__NATIVE_USE_KLIB__))
  malloc_block_t *block;

  if (ptr == NULL) return;
  if (malloc_head == NULL) return;

  block = (malloc_block_t *)ptr - 1;
  block->free = 1;
  coalesce_blocks();
#else
  (void)ptr;
#endif
}

#endif
