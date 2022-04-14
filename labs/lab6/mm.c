/****************************************************************************
 *                                                                          *
 *   mm.c                                                                   *
 *   Memory Allocator with Segmented AVL Tree                               *
 *                                                                          *
 *   CS230: System Programming                                              *
 *   20200434 Lee, Giyeon                                                   *
 *                                                                          *
 ****************************************************************************/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memlib.h"
#include "mm.h"

/////////////////////////////////// Macros ///////////////////////////////////

/*************
 * Constants *
 *************/
#define ALIGNMENT 8         // single word (4) or double word (8) alignment
#define WSIZE 4             // Word and header/footer size (bytes)
#define DSIZE 8             // Double word size (bytes)
#define CHUNKSIZE (1 << 12) // Extend heap by this amount (bytes)
#define MIN_BLK_SIZE 24     // Minimum block size
#define NUM_TREE 20         // Number of segmented trees
#define OVERHEAD (1 << 8)   // Bound for coalesce (realloc)

/***********************
 * Alignment Functions *
 ***********************/
// Align to nearest multiple of ALIGNMENT
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)
#define SIZE_T_SIZE (ALIGN (sizeof (size_t)))

/*********************
 * Basic comparators *
 *********************/
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

/*******************************
 * Block Header/Footer Related *
 *******************************/
// Read and write a word at address p
#define GET(p) (*(unsigned int *) (p))
#define PUT(p, val) (*(unsigned int *) (p) = (val))
#define GET_PTR(ptr) ((unsigned int *) (unsigned int) GET (ptr))
#define PUT_PTR(p, ptr) (*(unsigned int *) (p) = (unsigned int) (ptr))
// Pack a size and 1 bit into a word
#define PACK(size, alloc) ((size) | (alloc))
// Read the size and 1 fields from address p
#define GET_SIZE(p) (GET (p) & ~0x7)
#define GET_ALLOC(p) (GET (p) & 0x1)
// Set / clear RA tag
#define SET_RA(p) (*(unsigned int *) (p) = GET (p) | 0x2)
#define CLR_RA(p) (*(unsigned int *) (p) = GET (p) & ~0x2)
// Given block ptr bp, compute address of its header and footer
#define HDRP(bp) ((char *) (bp) -WSIZE)
#define FTRP(bp) ((char *) (bp) + GET_SIZE (HDRP (bp)) - DSIZE)
// Given block ptr bp, compute address of next and previous blocks
#define PREV_BLKP(bp) ((char *) (bp) -GET_SIZE (((char *) (bp) -DSIZE)))
#define NEXT_BLKP(bp) ((char *) (bp) + GET_SIZE (((char *) (bp) -WSIZE)))

/****************
 * Tree Related *
 ****************/
// Left and right child of a node
#define LEFT_CHILD(ptr) ((char *) (ptr))
#define RIGHT_CHILD(ptr) ((char *) (ptr) + WSIZE)
// Height of a tree
#define HEIGHT(ptr) ((char *) (ptr) + 2 * WSIZE)
#define GET_HEIGHT(ptr) ((ptr == NULL) ? 0 : GET (HEIGHT (ptr)))

////////////////////////////// Global Variables //////////////////////////////

static char *heap_listp; // Pointer to first block on heap
static char *free_treep; // Pointer to AVL trees

////////////////////////////// Inline Functions //////////////////////////////
/* Some of the code used in implementing the AVL tree was written with some
 * reference to open source examples */

// Allows array-like notations for tree ptr
static inline char *
AVL_tree (int idx)
{
    return (char *) (free_treep) + (idx * WSIZE);
}

// Initialize the given tree node
// left, right = NULL
// height = 1
static inline void
init_node (void *ptr, size_t size)
{
    PUT (HDRP (ptr), PACK (size, 0));
    PUT (FTRP (ptr), PACK (size, 0));
    PUT_PTR (RIGHT_CHILD (ptr), NULL);
    PUT_PTR (LEFT_CHILD (ptr), NULL);
    PUT (HEIGHT (ptr), 1);
}

// Refresh the height of trees
static inline void
refresh_height (void *node)
{
    PUT (HEIGHT (node),
         1
             + MAX (GET_HEIGHT (GET_PTR (LEFT_CHILD (node))),
                    GET_HEIGHT (GET_PTR (RIGHT_CHILD (node)))));
}

// Left rotate AVL tree at given node
static inline void *
L_rotate (void *ptr)
{
    void *R_child  = GET_PTR (RIGHT_CHILD (ptr));
    void *RL_child = GET_PTR (LEFT_CHILD (R_child));

    PUT_PTR (LEFT_CHILD (R_child), ptr);
    PUT_PTR (RIGHT_CHILD (ptr), RL_child);

    refresh_height (ptr);
    refresh_height (R_child);

    return R_child;
}

// Right roatate AVL tree at given node
static inline void *
R_rotate (void *ptr)
{
    void *L_child  = GET_PTR (LEFT_CHILD (ptr));
    void *LR_child = GET_PTR (RIGHT_CHILD (L_child));

    PUT_PTR (RIGHT_CHILD (L_child), ptr);
    PUT_PTR (LEFT_CHILD (ptr), LR_child);

    refresh_height (ptr);
    refresh_height (L_child);

    return L_child;
}

// Double left rotate AVL tree at given node
static inline void *
LL_rotate (void *ptr)
{
    void *right = GET_PTR (RIGHT_CHILD (ptr));
    PUT_PTR (RIGHT_CHILD (ptr), R_rotate (right));
    return L_rotate (ptr);
}

// Double right rotate AVL tree at given node
static inline void *
RR_rotate (void *ptr)
{
    void *left = GET_PTR (LEFT_CHILD (ptr));
    PUT_PTR (LEFT_CHILD (ptr), L_rotate (left));
    return R_rotate (ptr);
}

// Balance the given AVL tree
static inline void *
balance (void *root)
{
    void *left         = GET_PTR (LEFT_CHILD (root));
    void *right        = GET_PTR (RIGHT_CHILD (root));
    int balance_factor = (int) GET_HEIGHT (right) - (int) GET_HEIGHT (left);

    // Right heavy
    if (balance_factor == 2)
    {
        if (GET_HEIGHT (GET_PTR (LEFT_CHILD (right)))
            > GET_HEIGHT (GET_PTR (RIGHT_CHILD (right))))
        {
            root = LL_rotate (root);
        } else
        {
            root = L_rotate (root);
        }
    }
    // Left heavy
    if (balance_factor == -2)
    {
        if (GET_HEIGHT (GET_PTR (RIGHT_CHILD (left)))
            > GET_HEIGHT (GET_PTR (LEFT_CHILD (left))))
        {
            root = RR_rotate (root);
        } else
        {
            root = R_rotate (root);
        }
    }

    return root;
}

// Return index value that matches the size
static inline int
get_idx (size_t size)
{
    int idx = 0;

    while ((idx < NUM_TREE - 1) && ((size > 1) || AVL_tree (idx) == NULL))
    {
        size >>= 1;
        idx++;
    }
    return idx;
}

/////////////////////////// Function Declarations ///////////////////////////

static void *extend_heap (size_t);
static void *coalesce (void *);
static void *find_fit (size_t);
static void rec_find_fit (void *, size_t, void **);
static void *place (void *, size_t);
static void insert_node (void *, size_t);
static void *rec_insert_node (void *, void *);
static void delete_node (void *);
static void *rec_delete_node (void *, void *);

//////////////////////////// Helper functions ////////////////////////////

/*
 * extend_heap - Extends heap by adding a free block
 */
static void *
extend_heap (size_t size)
{
    char *bp;
    size_t asize;

    /* Allocate an even number of words to maintain alignment */
    asize = ALIGN (size) * WSIZE; /* Maintain alignment*/
    if ((bp = mem_sbrk (asize)) == (void *) -1)
    {
        return NULL;
    }
    /* Initialize free block header/footer and the epilogue header */
    PUT (HDRP (bp), PACK (asize, 0));         /* free block header */
    PUT (FTRP (bp), PACK (asize, 0));         /* free block footer */
    PUT (HDRP (NEXT_BLKP (bp)), PACK (0, 1)); /* new epilogue header */
    /* coalesce bp with next and previous blocks */
    return coalesce (bp);
}

/*
 * coalesce - Coalesce adjacent free blocks if possible
 */
static void *
coalesce (void *ptr)
{
    char *prev_blk    = PREV_BLKP (ptr);
    char *next_blk    = NEXT_BLKP (ptr);
    size_t prev_alloc = GET_ALLOC (FTRP (prev_blk));
    size_t next_alloc = GET_ALLOC (HDRP (next_blk));
    size_t size       = GET_SIZE (HDRP (ptr));

    // CASE 1
    if (prev_alloc && next_alloc)
    {
        insert_node (ptr, size);
        return ptr;
    }
    // CASE 2: Merge prev_blk
    if (!prev_alloc && next_alloc)
    {
        size += GET_SIZE (HDRP (prev_blk));
        delete_node (prev_blk);
        insert_node (prev_blk, size);
        ptr = prev_blk;
        return ptr;
    }
    // CASE 3: Merge next_blk
    else if (prev_alloc && !next_alloc)
    {
        size += GET_SIZE (HDRP (next_blk));
        delete_node (next_blk);
        insert_node (ptr, size);
        return ptr;
    }
    // CASE 4: Merge both prev and next blk
    else
    {
        size += GET_SIZE (HDRP (prev_blk)) + GET_SIZE (HDRP (next_blk));
        delete_node (next_blk);
        delete_node (prev_blk);
        insert_node (prev_blk, size);
        ptr = prev_blk;
        return ptr;
    }
}

/*
 * find_fit - Find a fit for a block from seg trees (wrapper)
 */
static void *
find_fit (size_t size)
{
    int idx;
    void *ptr = NULL;

    // Iterate through trees
    for (idx = get_idx (size); idx < NUM_TREE; idx++)
    {
        void *tree = AVL_tree (idx);
        rec_find_fit (GET_PTR (tree), size, &ptr);
        if (ptr != NULL)
            break;
    }

    if (ptr == NULL)
        return ptr;
    else
        return place (ptr, size);
}

/*
 * rec_find_fit - Recursively find a fit for a blk in tree
 */
static void
rec_find_fit (void *root, size_t query, void **blk_ptr)
{
    if (root == NULL)
        return;

    size_t size = GET_SIZE (HDRP (root));
    void *left  = GET_PTR (LEFT_CHILD (root));
    void *right = GET_PTR (RIGHT_CHILD (root));

    rec_find_fit (left, query, blk_ptr);

    if (size >= query)
    {
        if ((*blk_ptr == NULL)
            || (GET_SIZE (HDRP (*blk_ptr)) - query) > (size - query))
            *blk_ptr = root;
    }

    rec_find_fit (right, query, blk_ptr);
}

/*
 * place - Set headers and footers for new 1 blocks, split blocks if
 * space is enough
 */
static void *
place (void *ptr, size_t asize)
{
    size_t psize = GET_SIZE (HDRP (ptr));
    size_t diff  = psize - asize;

    delete_node (ptr);
    // Do not split
    if (diff <= MIN_BLK_SIZE)
    {
        PUT (HDRP (ptr), PACK (psize, 1));
        PUT (FTRP (ptr), PACK (psize, 1));
        return ptr;
    }
    // Split
    else if (asize >= 100)
    {
        PUT (HDRP (ptr), PACK (diff, 0));
        PUT (FTRP (ptr), PACK (diff, 0));
        insert_node (ptr, diff);
        ptr = NEXT_BLKP (ptr);
        PUT (HDRP (ptr), PACK (asize, 1));
        PUT (FTRP (ptr), PACK (asize, 1));
        return ptr;
    } else
    {
        PUT (HDRP (ptr), PACK (asize, 1));
        PUT (FTRP (ptr), PACK (asize, 1));
        PUT (HDRP (NEXT_BLKP (ptr)), PACK (diff, 0));
        PUT (FTRP (NEXT_BLKP (ptr)), PACK (diff, 0));
        insert_node (NEXT_BLKP (ptr), diff);
        return ptr;
    }
}

///////////////////////// Helper Functions for Trees /////////////////////////

/*
 * insert_node - Insert a node to a tree (wrapper)
 */
static void
insert_node (void *ptr, size_t size)
{
    void *tree = AVL_tree (get_idx (size));

    init_node (ptr, size);
    PUT_PTR (tree, rec_insert_node (GET_PTR (tree), ptr));
}

/*
 * rec_insert_node - Recursive function for node insertion
 */
static void *
rec_insert_node (void *root, void *ptr)
{

    if (root == NULL)
        return ptr;

    void *left  = LEFT_CHILD (root);
    void *right = RIGHT_CHILD (root);

    if (ptr < root)
        PUT_PTR (left, rec_insert_node (GET_PTR (left), ptr));
    else if (ptr > root)
        PUT_PTR (right, rec_insert_node (GET_PTR (right), ptr));

    refresh_height (root);
    root = balance (root);
    return root;
}

/*
 * delete_node - Delete a node from tree (wraper)
 */
static void
delete_node (void *ptr)
{
    size_t size = GET_SIZE (HDRP (ptr));
    void *tree  = AVL_tree (get_idx (size));
    PUT_PTR (tree, rec_delete_node (GET_PTR (tree), ptr));
}

/*
 * rec_delete_node - Recursive function for node deletion
 */
static void *
rec_delete_node (void *root, void *ptr)
{
    if (root == NULL)
        return root;

    void *left      = LEFT_CHILD (root);
    void *right     = RIGHT_CHILD (root);
    void *left_ptr  = GET_PTR (left);
    void *right_ptr = GET_PTR (right);

    if (ptr < root)
        PUT_PTR (left, rec_delete_node (left_ptr, ptr));
    else if (ptr > root)
        PUT_PTR (right, rec_delete_node (right_ptr, ptr));
    else
    {
        if (!left_ptr && !right_ptr)
            return NULL;
        else if (left_ptr && !right_ptr)
            root = left_ptr;
        else if (!left_ptr && right_ptr)
            root = right_ptr;
        else
        {
            void *minimum;
            void *curr = GET_PTR (right);
            while ((minimum = GET_PTR (LEFT_CHILD (curr))) != NULL)
                curr = minimum;

            PUT_PTR (right, rec_delete_node (GET_PTR (right), curr));
            root = curr;
            PUT_PTR (RIGHT_CHILD (root), GET_PTR (right));
            PUT_PTR (LEFT_CHILD (root), GET_PTR (left));
        }
    }

    refresh_height (root);
    root = balance (root);

    return root;
}

///////////////////////// End of Helper functions /////////////////////////

/*
 * mm_init - initialize the malloc package.
 */
int
mm_init (void)
{
    int idx;

    /* Allocate memory for initial AVL trees */
    if ((free_treep = mem_sbrk (NUM_TREE * WSIZE)) == (void *) -1)
        return -1;

    for (idx = 0; idx < NUM_TREE; idx++)
    {
        PUT_PTR (AVL_tree (idx), NULL);
    }

    /* Allocate memory for the initial empty heap */
    if ((heap_listp = mem_sbrk (4 * WSIZE)) == (void *) -1)
        return -1;

    PUT (heap_listp, 0);                             /* Alignment padding */
    PUT (heap_listp + (1 * WSIZE), PACK (DSIZE, 1)); /* Prologue header */
    PUT (heap_listp + (2 * WSIZE), PACK (DSIZE, 1)); /* Prologue footer */
    PUT (heap_listp + (3 * WSIZE), PACK (0, 1));     /* Epilogue header */
    heap_listp += DSIZE;

    /* Extend the empty heap */
    if (extend_heap (CHUNKSIZE / WSIZE) == NULL)
        return -1;
    return 0;
}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *
mm_malloc (size_t size)
{
    size_t asize;      /* Adjusted block size */
    size_t extendsize; /* Amount to extend heap if no fit */
    void *ptr = NULL;

    /* Ignore spurious cases */
    if (size == 0)
        return NULL;

    /* Adjust block size to include overhead and alignment reqs */
    asize = MAX (ALIGN (size + SIZE_T_SIZE), MIN_BLK_SIZE);

    /* Search the seglist for a fit */
    ptr = find_fit (asize);

    /* No fit found. Get more memory and place the block */
    if (ptr == NULL)
    {
        extendsize = MAX (asize, CHUNKSIZE);
        if ((ptr = extend_heap (extendsize / WSIZE)) == NULL)
            return NULL;
        ptr = place (ptr, asize);
        return ptr;
    }
    return ptr;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void
mm_free (void *ptr)
{
    size_t size = GET_SIZE (HDRP (ptr));
    PUT (HDRP (ptr), PACK (size, 0));
    PUT (FTRP (ptr), PACK (size, 0));
    coalesce (ptr);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 *              divided into sevral cases
 *              Rollback to previous version
 */
void *
mm_realloc (void *ptr, size_t size)
{
    char *oldptr = ptr;
    char *newptr;
    size_t copy_size;
    size_t old_size      = GET_SIZE (HDRP (oldptr));
    size_t old_next_size = GET_SIZE (HDRP (NEXT_BLKP (oldptr)));

    if (!GET_ALLOC (HDRP (NEXT_BLKP (oldptr))) && old_size + old_next_size >= size)
    {
        size_t asize;

        if (size == 0)
            return NULL;

        if (size <= MIN_BLK_SIZE)
            asize = MIN_BLK_SIZE;
        else
            asize = ALIGN (size + DSIZE);

        delete_node (NEXT_BLKP (oldptr));

        if ((old_size + old_next_size - asize) >= (2 * DSIZE))
        {
            PUT (HDRP (ptr), PACK (asize, 1));
            PUT (FTRP (ptr), PACK (asize, 1));

            newptr = NEXT_BLKP (ptr);
            PUT (HDRP (newptr), PACK (old_size + old_next_size - asize, 0));
            PUT (FTRP (newptr), PACK (old_size + old_next_size - asize, 0));

        } else
        {
            PUT (HDRP (ptr), PACK (old_size + old_next_size, 1));
            PUT (FTRP (ptr), PACK (old_size + old_next_size, 1));
        }

        return ptr;
    } else
    {
        newptr = mm_malloc (size);

        if (newptr == NULL)
            return NULL;

        copy_size = GET_SIZE (HDRP (oldptr));
        if (size < copy_size)
            copy_size = size;

        memcpy (newptr, oldptr, copy_size);
        mm_free (oldptr);

        return newptr;
    }
}
