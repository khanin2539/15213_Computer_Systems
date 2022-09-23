/*
 ******************************************************************************
 *                                   mm.c                                     *
 *           64-bit struct-based implicit free list memory allocator          *
 *                  15-213: Introduction to Computer Systems                  *
 *                                                                            *
 *  ************************************************************************  *
 *                  TODO: insert your documentation here. :)                  *
 *                                                                            *
 *  ************************************************************************  *
 *  ** ADVICE FOR STUDENTS. **                                                *
 *  Step 0: Please read the writeup!                                          *
 *  Step 1: Write your heap checker. Write. Heap. checker.                    *
 *  Step 2: Place your contracts / debugging assert statements.               *
 *  Good luck, and have fun!                                                  *
 *                                                                            *
 ******************************************************************************
 */

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memlib.h"
#include "mm.h"

/* Do not change the following! */

#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#define memset mem_memset
#define memcpy mem_memcpy
#endif /* def DRIVER */

/* You can change anything from here onward */

/*
 * If DEBUG is defined (such as when running mdriver-dbg), these macros
 * are enabled. You can use them to print debugging output and to check
 * contracts only in debug mode.
 *
 * Only debugging macros with names beginning "dbg_" are allowed.
 * You may not define any other macros having arguments.
 */
#ifdef DEBUG
/* When DEBUG is defined, these form aliases to useful functions */
#define dbg_printf(...) printf(__VA_ARGS__)
#define dbg_requires(expr) assert(expr)
#define dbg_assert(expr) assert(expr)
#define dbg_ensures(expr) assert(expr)
#define dbg_printheap(...) print_heap(__VA_ARGS__)
#else
/* When DEBUG is not defined, no code gets generated for these */
/* The sizeof() hack is used to avoid "unused variable" warnings */
#define dbg_printf(...) (sizeof(__VA_ARGS__), -1)
#define dbg_requires(expr) (sizeof(expr), 1)
#define dbg_assert(expr) (sizeof(expr), 1)
#define dbg_ensures(expr) (sizeof(expr), 1)
#define dbg_printheap(...) ((void)sizeof(__VA_ARGS__))
#endif

#define ALIGNMENT 16

/* Basic constants */
typedef uint64_t word_t;
static const size_t wsize = sizeof(word_t);
static const size_t dsize = 2 * wsize;
static const size_t min_block_size = dsize;
static const size_t chunksize = (1 << 12);
static const int num_free_list = 16; // class number of free lists
// masks for header bit manipulation
static const word_t alloc_mask = 0x1;
static const word_t prev_alloc_mask = 0x2;
static const word_t dsize_block = 0x4;
static const word_t size_mask = ~(word_t)0xF;

typedef struct block {
  // Header has allocation and size flags
  word_t header;

  char payload[0];

} block_t;

/* Global variables */
/* Pointer to first block */
static block_t *heap_start = NULL;
/*Pointer to Free Lists*/
static block_t **free_list = NULL;
/* Pointer to Prologue Footer */
static word_t *prolog_footer = NULL;
/* Pointer to Epilogue Header */
static word_t *epi_header = NULL;

/*True if last block in the heap is allocated. False if it's free*/
static bool is_allo_last = false;

bool mm_checkheap(int lineno);

/* Function prototypes for internal helper routines */
static block_t *extend_heap(size_t size);
static void fill(block_t *block, size_t asize);
static block_t *find_fit(size_t asize);
static block_t *coalesce(block_t *block);

static size_t max(size_t x, size_t y);
static size_t round_up(size_t size, size_t n);
static word_t pack(size_t size, bool alloc, bool prev_alloc, bool small_block);

static size_t get_size(block_t *block);
static size_t get_payload_size(block_t *block);
static size_t extract_size(word_t header);

static bool extract_alloc(word_t header);
static bool get_alloc(block_t *block);

static bool extract_prev_alloc(word_t header);
static bool get_prev_alloc(block_t *block);

static bool extract_small_block(word_t header);
static bool get_dsize_block(block_t *block);

static void write_header(block_t *block, size_t size, bool small_block,
                         bool prev_alloc, bool alloc);
static void write_footer(block_t *block, size_t size, bool small_block,
                         bool prev_alloc, bool alloc);

static block_t *get_next(block_t *block);
static void set_next(block_t *block, block_t *succ);

static block_t *get_prev(block_t *block);
static void set_next(block_t *block, block_t *pred);

static block_t *payload_to_header(void *bp);
static void *header_to_payload(block_t *block);

static block_t *find_next(block_t *block);
static word_t *find_prev_footer(block_t *block);
static block_t *find_prev(block_t *block);

static void insert_freelist(block_t *block);
static void remove_freelist(block_t *block);

static int find_class_size(size_t asize);

/*
 * mm_init: initialises the heap and global variables. returns true on
 *			successful initialization.
 */
bool mm_init(void) {
  // make 16 blocks of free lists which has 16 classes
  word_t *sizeclass = (word_t *)(mem_sbrk(16 * wsize));

  if (sizeclass == (void *)-1) {
    return false;
  }
  // make the first freelist point to the first size class
  free_list = (block_t **)&sizeclass[0];
  // empty contents of free_list
  for (int i = 0; i < num_free_list; i++) {
    free_list[i] = NULL;
  }

  // Create the initial empty heap
  word_t *start = (word_t *)(mem_sbrk(2 * wsize));

  if (start == (void *)-1) {
    return false;
  }

  start[0] = pack(0, true, false, false); // Prologue footer: size 0, allocated,
                                          // no pre alloc and no dsize block
  start[1] = pack(0, true, false, false); // Epilogue header: size 0, allocated,
                                          // no pre alloc and no dsize block

  prolog_footer = &start[0];
  epi_header = &start[1];

  // Heap starts with first "block header", currently the epilogue footer
  heap_start = (block_t *)&(start[1]);
  is_allo_last = true;

  // Extend the empty heap with a free block of chunksize bytes
  if (extend_heap(chunksize) == NULL) {
    return false;
  }

  return true;
}

/*
 * mm_malloc: returns a pointer to an allocated block payload
 */
void *malloc(size_t size) {
  dbg_requires(mm_checkheap(__LINE__));

  size_t asize;      // Adjusted block size
  size_t extendsize; // Amount to extend heap if no fit is found
  block_t *block;
  void *bp = NULL;

  if (heap_start == NULL) // Initialize heap if it isn't initialized
  {
    mm_init();
  }

  if (size == 0) // Ignore spurious request
  {
    dbg_ensures(mm_checkheap(__LINE__));
    return bp;
  }

  // Adjust block size to include overhead and to meet alignment requirements
  asize = round_up(size + wsize, dsize);

  // Search the free list for a fit
  block = find_fit(asize);

  // If no fit is found, request more memory, and then and place the block
  if (block == NULL) {
    extendsize = max(asize, chunksize);
    block = extend_heap(extendsize);
    if (block == NULL) // extend_heap returns an error
    {
      return bp;
    }
  }
  // fill the block to the heap
  fill(block, asize);
  // make it point to payload
  bp = header_to_payload(block);

  if (get_size(find_next(block)) == 0) {
    // the last block is allocated
    is_allo_last = true;
  }

  dbg_ensures(mm_checkheap(__LINE__));
  return bp;
}

/*
 * mm_free: frees the block led by pointer bp. it also coalese if propetieis
 * satisfied.
 */
void free(void *bp) {
  dbg_requires(mm_checkheap(__LINE__));

  if (bp == NULL) {
    return;
  }

  block_t *block = payload_to_header(bp);
  size_t size = get_size(block);
  bool prev_alloc = get_prev_alloc(block);
  bool small_block;

  if (size == dsize) {
    small_block = true;
  } else {
    small_block = false;
  }

  write_header(block, size, small_block, prev_alloc, false);
  write_footer(block, size, small_block, prev_alloc, false);

  coalesce(block);

  dbg_ensures(mm_checkheap(__LINE__));
  return;
}

/*
 * mm_realloc: returns pointer to an allocated region of atleast size bytes.
 *			   calls malloc(size) if ptr is NULL. calls free(ptr) if
 *size is 0. if ptr is not NULL, it must have been returned by an earlier call
 *to malloc or realloc and not yet have been freed. the call to realloc takes an
 *existing block of memory, pointed to by ptr — the old block. upon return,
 *contents of the new block are same as those of the old block, up to minimum of
 *the old and new sizes. everything else is uninitialized.
 */
void *realloc(void *ptr, size_t size) {
  dbg_requires(mm_checkheap(__LINE__));
  block_t *block = payload_to_header(ptr);
  size_t copysize;
  void *newptr;

  // If size == 0, then free block and return NULL
  if (size == 0) {
    free(ptr);
    return NULL;
  }

  // If ptr is NULL, then equivalent to malloc
  if (ptr == NULL) {
    return malloc(size);
  }

  // Otherwise, proceed with reallocation
  newptr = malloc(size);
  // If malloc fails, the original block is left untouched
  if (newptr == NULL) {
    return NULL;
  }

  // Copy the old data
  copysize = get_payload_size(block); // gets size of old payload
  if (size < copysize) {
    copysize = size;
  }
  memcpy(newptr, ptr, copysize);

  // Free the old block
  free(ptr);

  dbg_ensures(mm_checkheap(__LINE__));
  return newptr;
}

/*
 * mm_calloc: allocates memory for an array of elements of size bytes each and
 *			  returns a pointer to the allocated memory. the memory
 *is set to zero before returning.
 */
void *calloc(size_t elements, size_t size) {
  dbg_requires(mm_checkheap(__LINE__));
  void *bp;
  size_t asize = elements * size;

  if (asize / elements != size)
    // Multiplication overflowed
    return NULL;

  bp = malloc(asize);
  if (bp == NULL) {
    return NULL;
  }
  // Initialize all bits to 0
  memset(bp, 0, asize);

  dbg_ensures(mm_checkheap(__LINE__));
  return bp;
}

/******** The remaining content below are helper and debug routines ********/

/*
 * extend_heap: extend the heap by with free block and return its block pointer.
 *				r
 */
static block_t *extend_heap(size_t size) {
  void *bp;

  // Allocate an even number of words to maintain alignment
  size = round_up(size, dsize);
  if ((bp = mem_sbrk(size)) == (void *)-1) {
    return NULL;
  }

  // Initialize free block header/footer
  block_t *block = payload_to_header(bp);
  write_header(block, size, false, is_allo_last, false);
  write_footer(block, size, false, is_allo_last, false);

  // The last block in the heap is now free
  is_allo_last = false;

  // Create new epilogue header
  block_t *block_next = find_next(block);
  write_header(block_next, 0, false, false, true);
  epi_header = (word_t *)block_next;

  // Coalesce in case the previous block was free
  return coalesce(block);
}

static block_t *coalesce(block_t *block) {
  // prepare next block
  block_t *block_next = find_next(block);
  // prepare status of pre alloc
  bool prev_alloc = get_prev_alloc(block);
  // check the next alloc status
  bool next_alloc = get_alloc(block_next);
  size_t size = get_size(block);

  if (get_size(block_next) == 0) {

    is_allo_last = false;
  }

  if (prev_alloc && next_alloc) // Case 1
  {
    // add block to free list
    insert_freelist(block);

    // update the previous block with allocated bit from next block
    if (get_size(block_next) != 0) {
      write_header(block_next, get_size(block_next),
                   get_dsize_block(block_next), false, true);
    }
  }

  else if (prev_alloc && !next_alloc) // Case 2
  {
    size += get_size(block_next); // get combined size from 2 blocks

    remove_freelist(block_next);
    // mark the header and footer of 2 blocks size as free
    write_header(block, size, false, prev_alloc, false);
    write_footer(block, size, false, prev_alloc, false);
    // insert that freed block to the list
    insert_freelist(block);
  }

  else if (!prev_alloc && next_alloc) // Case 3
  {
    block_t *block_prev = find_prev(block);
    // get prev info pre-allocated
    bool prev_block_alloc = get_prev_alloc(block_prev);
    size += get_size(block_prev); // get combined size from 2 blocks

    // remove the unallocated block from free_list
    remove_freelist(block_prev);

    // mark the header and footer of 2 blocks size as free
    write_header(block_prev, size, false, prev_block_alloc, false);
    write_footer(block_prev, size, false, prev_block_alloc, false);

    block = block_prev;
    insert_freelist(block);
    // handle case when the block is at the top
    if (get_size(block_next) != 0) {
      // update header with block next, size of blok next,
      // is the block dsize?, prev alloc? and allocated?
      write_header(block_next, get_size(block_next),
                   get_dsize_block(block_next), false, true);
    }
  }

  else // Case 4
  {
    block_t *block_prev = find_prev(block);
    bool prev_block_alloc = get_prev_alloc(block_prev);
    size += get_size(block_next) + get_size(block_prev);

    // remove the prev and next blocks  (neigboring )
    remove_freelist(block_prev);
    remove_freelist(block_next);

    // update the footer/header of the updated block
    write_header(block_prev, size, false, prev_block_alloc, false);
    write_footer(block_prev, size, false, prev_block_alloc, false);

    // move the current block to the orev position
    block = block_prev;

    // insert the new coalesed block to the free list
    insert_freelist(block);
  }

  return block;
}

/*
 * fill: fills the block in the heap. when difference between asize and size
 of block > minimum block size, the block is placed.
 otherwise the block is placed and add the remaining block to the free list.
 */
static void fill(block_t *block, size_t asize) {
  size_t resize = get_size(block);
  size_t remain = resize - asize;
  // when the resize = allocated size > min block size
  // remove the current block
  if ((remain) >= min_block_size) {
    block_t *block_next;

    remove_freelist(block);

    if (asize == dsize) {
      // update header info of 16 byte
      write_header(block, asize, true, get_prev_alloc(block), true);
    } else {
      // update header info of more than 16 byte
      write_header(block, asize, false, get_prev_alloc(block), true);
    }

    // get the next block
    // update header and footer of the next block
    // for dsize block flag for small block is true
    block_next = find_next(block);
    if (remain == dsize) {
      write_header(block_next, remain, true, true, false);
      write_footer(block_next, remain, true, true, false);

      // for block size > dsize block flag for small block is false
    } else {
      write_header(block_next, remain, false, true, false);
      write_footer(block_next, remain, false, true, false);
    }

    insert_freelist(block_next);
  }

  else {
    block_t *block_next = find_next(block);

    remove_freelist(block);

    if (asize == dsize) {
      // update header info of 16 byte
      write_header(block, resize, true, get_prev_alloc(block), true);
    } else {
      // update header info of more than 16 byte
      write_header(block, resize, false, get_prev_alloc(block), true);
    }

    // make change of the prev block with size and pre allocated bit
    if (get_size(block_next) != 0) {
      write_header(block_next, get_size(block_next),
                   get_dsize_block(block_next), true, true);
    }
  }

  return;
}

/*
 * find_fit: use best fit to find a free block matching
  the size of the allocated block. return NULL if not found.
 */
static block_t *find_fit(size_t asize) {
  block_t *block;
  // start index with the block size first
  int index = find_class_size(asize);
  // use find_class to get minimum required size class for block

  // traverse through size classes greater than or equal to index until
  // appropriate block is found
  while (index < num_free_list) {
    for (block = free_list[index]; block != NULL; block = get_next(block)) {

      // if lucky that block = asize block return block
      if (asize == get_size(block)) {
        return block;
      }

      // when block > asize, find the one from the index
      // that has the best fit
      if (asize < get_size(block)) {

        block_t *next = get_next(block);
        // until next is NULL and size of block-asize > next-asize
        while (next != NULL &&
               (get_size(block) - asize) > (get_size(next) - asize)) {
          block = next;
          next = get_next(next);
        }
        return block;
      }
    }
    index += 1;
  }

  return NULL; // no fit found
}

/*
 *
 */
bool mm_checkheap(int line) {
  int counter_free = 0; // keeping track of free blocks

  // check val of epi header (1)
  if (*epi_header != 1) {
    printf("%d: failed at Epilogue header\n", line);
    return false;
  }

  // check val of prol footer (1)
  if (*prolog_footer != 1) {
    printf("%d: failed at Prologue  footer\n", line);
    return false;
  }

  // For each block in the heap
  for (block_t *block = heap_start; get_size(block) != 0;
       block = find_next(block)) {

    // Payload address bust be 16 bytes aligned
    if ((word_t)header_to_payload(block) % ALIGNMENT != 0) {
      printf("%d: failed at Payload Alignment\n", line);
      return false;
    }

    if ((char *)epi_header <= ((char *)block + get_size(block))) {
      printf("%d: he block outside heap boundaries: above\n", line);
      return false;
    }

    if ((block_t *)prolog_footer > block) {
      printf("%d: The block outside heap boundaries: below \n", line);
      return false;
    }

    // Bcheck if block size > than minimum block size &&
    // = divisible by ALIGNMENT
    if (get_size(block) % ALIGNMENT != 0 &&
        (get_size(block) < min_block_size)) {
      printf("%d: invalid block size\n", line);
      return false;
    }

    // check if the alloc bit of the curr block is matching
    // between get alloc of current block and the next block
    if (get_prev_alloc(find_next(block)) != get_alloc(block)) {
      if (get_size(find_next(block)) != 0) {
        printf("%d: inconsistent alloc bits \n", line);
        return false;
      }
    }
  }

  // looping through individual free lists
  // important
  for (int i = 0; i < num_free_list; i++) {
    for (block_t *block = free_list[i]; block != NULL;
         block = get_next(block)) {
      // begin decrement number of free list
      --counter_free;

      // check if the size from index matches with the one
      // from the list
      if (i != find_class_size(get_size(block))) {
        printf("%d: unmatching size class\n", line);
        return false;
      }

      // check if the curr block == next of prev block
      if (block != get_prev(get_next(block)) && get_next(block) != NULL) {
        printf("%d: invalid  prev-curr block sequence\n", line);
        return false;
      }

      // check if the prev block == prev of curr block
      if (block != get_next(get_prev(block)) && get_prev(block) != NULL) {
        printf("%d: invalid  prev-curr block sequencnce", line);
        return false;
      }

      // check if block lies exceeding epilogue header using pointer
      if ((char *)epi_header <= ((char *)block + get_size(block))) {
        printf("%d: Block lies outside heap boundaries: outside header\n",
               line);
        return false;

        // block can not lie before prologue footer
        if (block <= (block_t *)prolog_footer) {
          printf("%d: Block lies outside heap boundaries: outside footer\n",
                 line);
          return false;
        }
      }
    }
  }
  // check if the counter is 0 as it should be after
  // looping through freelist
  if (counter_free != 0) {
    printf("%d: invalid number of free blocks on free list\n", line);
    return false;
  }
  return true;
}
/*
 * get_prev: get the prev of block in the free list
 *
 */
static block_t *get_prev(block_t *block) {

  if (get_dsize_block(block)) {
    // get the payload as the address of the block
    word_t prev = *(word_t *)(header_to_payload(block));
    // remove pre-alloc and dsize flags by right and left shifts
    prev = (prev >> 3) << 3;
    return (block_t *)prev;
  }
  // when > 16 bytes, payload + 1 ( 4 bytes ) = prev header
  return *(block_t **)(((word_t *)header_to_payload(block)) + 1);
  // get_prev-footer  &(block->header)) - 1;
  // static void *header_to_payload(block_t *block) {
  // return (void *)(block->payload);
}

/*
 * get_next: returns the block next in the free list
 *			 returns NULL otherwise
 */
static block_t *get_next(block_t *block) {
  if (get_dsize_block(block)) {
    // contains 4 bytes
    word_t next = block->header;
    next = (next >> 3) << 3;
    return (block_t *)next;
  } else {
    return *(block_t **)(((word_t *)header_to_payload(block)));
  }
}

/*
 * next: set the next block from free list
   to the next block on heap.
 *
 */
static void set_next(block_t *block, block_t *next) {
  dbg_requires(block != NULL);
  if (get_dsize_block(block)) {
    bool prev_alloc = get_prev_alloc(block);
    word_t next_mask;
    if (prev_alloc) { // if the previoud block is allocated, assign the allo bit
      next_mask = ((word_t)next) | dsize_block | prev_alloc_mask;
    } else { // if the previous block is not allocated, assign dsize only
      next_mask = ((word_t)next) | dsize_block;
    }
    // assign the block header with the new next mask
    block->header = next_mask;
    return;
  }
  block_t **block_next = (block_t **)header_to_payload(block);
  *block_next = next;
  return;
}

/*
 * set_prev: set the prev block from free list
   to the prev block on heap.
 */
static void set_prev(block_t *block, block_t *prev) {
  dbg_requires(block != NULL);
  if (get_dsize_block(block)) {
    bool prev_alloc = get_prev_alloc(block);
    word_t prev_mask;
    // if the previoud block is allocated, assign the allo bit
    if (prev_alloc) {
      // assign the dsize status and prev_allocated by ORing masks
      prev_mask = ((word_t)prev) | dsize_block | prev_alloc_mask;
    } else { // if the previous block is not allocated, assign dsize only
      prev_mask = ((word_t)prev) | dsize_block;
    }
    block_t **prev_block = (block_t **)(header_to_payload(block));
    // assign only the bits of dsize block with prev mask
    *prev_block = (block_t *)prev_mask;
    return;
  }
  // if the block larger than dsize
  block_t **prev_block;
  prev_block = (block_t **)(((word_t *)header_to_payload(block)) + 1);
  *prev_block = prev;
  return;
}

/*
 * insert_freelist: inserts the free block into free list.
 *
 */
static void insert_freelist(block_t *block) {
  // i as the size class from the freelist
  int i = find_class_size(get_size(block));
  // when not NULL begin swap with the new free list
  // to the new head
  if (free_list[i] != NULL) {
    set_prev(block, NULL);
    set_next(block, free_list[i]);
    set_prev(free_list[i], block);
    free_list[i] = block;
    ;
  } else { // if not, make the current block as a new content of the freelist
    free_list[i] = block;
    // set the current block to NULL
    set_prev(block, NULL);
    set_next(block, NULL);
  }
}

/*
 * remove_freelist: removes freed block from the list.
 case1: set the next blockt otop when the block is top the list
 case2: remove the only block from the list
 case3: the block is at the end of list
 case4: link the prev and next as the middle is freed
 */
static void remove_freelist(block_t *block) {
  block_t *block_next = get_next(block);
  block_t *block_prev = get_prev(block);
  int i = find_class_size(get_size(block));

  if (block_next != NULL && block_prev == NULL) {
    free_list[i] = block_next;
    set_prev(block_next, NULL);
  }

  else if (block_next == NULL && block_prev == NULL) {
    free_list[i] = NULL;
  }

  else if (block_next == NULL && block_prev != NULL) {
    // set the next of the prev blocm to NULL
    set_next(block_prev, NULL);
  } else {
    set_next(block_prev, block_next);
    set_prev(block_next, block_prev);
  }
}

/*
 * find_class_size: returns the match index of free list.
 */
static int find_class_size(size_t asize) {
  if (asize >= 65536) {
    return 15;
  }
  if (asize >= 16384) {
    return 14;
  }
  if (asize >= 8192) {
    return 13;
  }
  if (asize >= 4096) {
    return 12;
  }
  if (asize >= 2048) {
    return 11;
  }
  if (asize >= 1024) {
    return 10;
  }
  if (asize >= 512) {
    return 9;
  }
  if (asize >= 256) {
    return 8;
  }
  if (asize >= 144) {
    return 7;
  }
  if (asize >= 128) {
    return 6;
  }
  if (asize >= 96) {
    return 5;
  }
  if (asize >= 80) {
    return 4;
  }
  if (asize >= 64) {
    return 3;
  }
  if (asize >= 48) {
    return 2;
  }
  if (asize >= 32) {
    return 1;
  } else {
    return 0;
  }
}

/*
 *****************************************************************************
 * The functions below are short wrapper functions to perform                *
 * bit manipulation, pointer arithmetic, and other helper operations.        *
 *                                                                           *
 * We've given you the function header comments for the functions below      *
 * to help you understand how this baseline code works.                      *
 *                                                                           *
 * Note that these function header comments are short since the functions    *
 * they are describing are short as well; you will need to provide           *
 * adequate details within your header comments for the functions above!     *
 *                                                                           *
 *                                                                           *
 * Do not delete the following super-secret(tm) lines!                       *
 *                                                                           *
 * 53 6f 20 79 6f 75 27 72 65 20 74 72 79 69 6e 67 20 74 6f 20               *
 *                                                                           *
 * 66 69 67 75 72 65 20 6f 75 74 20 77 68 61 74 20 74 68 65 20               *
 * 68 65 78 61 64 65 63 69 6d 61 6c 20 64 69 67 69 74 73 20 64               *
 * 6f 2e 2e 2e 20 68 61 68 61 68 61 21 20 41 53 43 49 49 20 69               *
 *                                                                           *
 *                                                                           *
 * 73 6e 27 74 20 74 68 65 20 72 69 67 68 74 20 65 6e 63 6f 64               *
 * 69 6e 67 21 20 4e 69 63 65 20 74 72 79 2c 20 74 68 6f 75 67               *
 * 68 21 20 2d 44 72 2e 20 45 76 69 6c 0a de ad be ef 0a 0a 0a               *
 *                                                                           *
 *****************************************************************************
 */

/*
 * max: returns x if x > y, and y otherwise.
 */
static size_t max(size_t x, size_t y) { return (x > y) ? x : y; }

/*
 * round_up: Rounds size up to next multiple of n
 */
static size_t round_up(size_t size, size_t n) {
  return (n * ((size + (n - 1)) / n));
}

/*
 * pack: returns a header reflecting a specified size, small block status,
 *		 previous block allocation status and its alloc status.
 *       If the block is allocated, the lowest bit is set to 1, and 0 otherwise
 *		 when the previous block is allocated, prev_alloc set to
 *1, otherwise 0. when block size is 16, third lowest bit is set to 1, and 0
 *otherwise
 */
static word_t pack(size_t size, bool alloc, bool prev_alloc, bool small_block) {
  if (small_block) {
    size = size | dsize_block;
  }
  if (prev_alloc) {
    size = size | prev_alloc_mask;
  }
  if (alloc) {
    size = size | alloc_mask;
  }
  return size;
}

/*
 * extract_size: returns the size of a given header value based on the header
 *               specification above.
 */
static size_t extract_size(word_t word) {
  if (word & dsize_block) {
    return dsize;
  }
  return (word & size_mask);
}

/*
 * get_size: returns the size of a given block by clearing the lowest 4 bits
 *           (as the heap is 16-byte aligned).
 */
static size_t get_size(block_t *block) { return extract_size(block->header); }

/*
 * get_payload_size: returns the payload size of a given block, equal to
 *                   the entire block size minus the header and footer sizes.
 */
static word_t get_payload_size(block_t *block) {
  size_t asize = get_size(block);
  return asize - wsize;
}

/*
 * extract_alloc: returns the allocation status of a given header value based
 *                on the header specification above.
 */
static bool extract_alloc(word_t word) { return (bool)(word & alloc_mask); }

/*
 * get_alloc: returns true when the block is allocated based on the
 *            block header's lowest bit, and false otherwise.
 */
static bool get_alloc(block_t *block) { return extract_alloc(block->header); }

/*
 * extract_prev_alloc: returns the previous block allocation status of
 *               a given header value based on the header
 *               specification above.
 */
static bool extract_prev_alloc(word_t word) {
  return (bool)(word & prev_alloc_mask);
}

/*
 * get_prev_alloc: returns true when the previous block is allocated based
 *            on the block header's second lowest bit, and false otherwise.
 */
static bool get_prev_alloc(block_t *block) {
  return extract_prev_alloc(block->header);
}

/*
 * extract_small_block: returns the small block status of
 *               a given header value based on the header
 *               specification above.
 */
static bool extract_small_block(word_t word) {
  return (bool)(word & dsize_block);
}

/*
 * get_dsize_block: returns true when the block size is 16, based
 *            on the block header's third lowest bit, and false otherwise.
 */
static bool get_dsize_block(block_t *block) {
  return extract_small_block(block->header);
}

/*
 * write_header: given a block and its size, previous block allocation status
 *				 and allocation status,
 *               writes an appropriate value to the block header.
 */
static void write_header(block_t *block, size_t size, bool small_block,
                         bool prev_alloc, bool alloc) {
  block->header = pack(size, alloc, prev_alloc, small_block);
}

/*
 * write_footer: given a block and its size, previous block allocation status
 *				 and allocation status, writes an appropriate
 *value to the block footer by first computing the position of the footer.
 */
static void write_footer(block_t *block, size_t size, bool small_block,
                         bool prev_alloc, bool alloc) {
  word_t *footerp = (word_t *)((block->payload) + get_size(block) - dsize);
  *footerp = pack(size, alloc, prev_alloc, small_block);
}

/*
 * find_next: returns the next consecutive block on the heap by adding the
 *            size of the block.
 */
static block_t *find_next(block_t *block) {
  dbg_requires(block != NULL);
  return (block_t *)(((char *)block) + get_size(block));

  // dbg_ensures(block_next != NULL);
}

/*
 * find_prev_footer: returns the footer of the previous block.
 */
static word_t *find_prev_footer(block_t *block) {
  // Compute previous footer position as one word before the header
  return (&(block->header)) - 1;
}

/*
 * find_prev: returns the previous block position by checking the previous
 *            block's footer and calculating the start of the previous block
 *            based on its size.
 */
static block_t *find_prev(block_t *block) {
  word_t *footerp = find_prev_footer(block);
  size_t size = extract_size(*footerp);
  return (block_t *)((char *)block - size);
}

/*
 * payload_to_header: given a payload pointer, returns a pointer to the
 *                    corresponding block.
 */
static block_t *payload_to_header(void *bp) {
  return (block_t *)(((char *)bp) - offsetof(block_t, payload));
}

/*
 * header_to_payload: given a block pointer, returns a pointer to the
 *                    corresponding payload.
 */
static void *header_to_payload(block_t *block) {
  return (void *)(block->payload);
}