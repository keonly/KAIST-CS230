/****************************************************************************
 *                                                                          *
 *   list.c                                                                 *
 *   Implementation of Doubly Linked List                                   *
 *                                                                          *
 *   CS230: System Programming                                              *
 *   20200434 Lee, Giyeon                                                   *
 *                                                                          *
 ****************************************************************************/

#include "list.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Allocate a linked list node with a given key
 * Allocate a node using malloc(),
 * initialize the pointers to NULL,
 * set the key value to the key provided by the argument
 */
list_node *
allocate_node_with_key (int key)
{
    list_node *alloc_node;
    alloc_node = (list_node *) malloc (sizeof (list_node));

    alloc_node->key  = key;
    alloc_node->prev = NULL;
    alloc_node->next = NULL;

    return alloc_node;
}

/*
 * Initialize the key values of the head/tail list nodes (I used -1 key values)
 * Also, link the head and tail to point to each other
 */
void
initialize_list_head_tail (list_node *head, list_node *tail)
{
    head->key  = -1;
    head->prev = tail;
    head->next = tail;

    tail->key  = -1;
    tail->prev = head;
    tail->next = head;
}

/*
 * Insert the *new_node* after the *node*
 */
void
insert_node_after (list_node *node, list_node *new_node)
{
    node->next->prev = new_node;
    new_node->next   = node->next;
    node->next       = new_node;
    new_node->prev   = node;
}

/*
 * Remove the *node* from the list
 * You may assume that *node* is neither NULL nor head node, nor tail node
 */
void
del_node (list_node *node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    free (node);
}

/*
 * Search from the head to the tail (excluding both head and tail,
 * as they do not hold valid keys), for the node with a given *search_key*
 * and return the node. If the node with given key is not present, return NULL.
 * You may assume that the list will only hold nodes with unique key values
 * (No duplicate keys in the list)
 */
list_node *
search_list (list_node *head, int search_key)
{
    list_node *iter = head->next;

    while (1)
    {
        if (iter->key == search_key)
            return iter;
        if (iter->next == head->prev)
            return NULL;
        iter = iter->next;
    }
}

/*
 * Count the number of nodes in the list (excluding head and tail),
 * and return the counted value
 */
int
count_list_length (list_node *head)
{
    int counter     = 0;
    list_node *iter = head;

    while (1)
    {
        if (iter->next == head->prev || iter->next == NULL)
            return counter;
        iter = iter->next;
        counter++;
    }
}

/*
 * Check if the list is empty (only head and tail exist in the list)
 * Return 1 if empty. Return 0 if list is not empty.
 */
int
is_list_empty (list_node *head)
{
    return (count_list_length (head) == 0);
}

/*
 * Loop through the list and print the key values
 * This function will not be tested, but it will aid you in debugging your list.
 * You may add calls to the *iterate_print_keys* function in the test.c
 * at points where you need to debug your list.
 * But make sure to test your final version with the original test.c code.
 */
void
iterate_print_keys (list_node *head)
{
    list_node *iter = head->next;

    while (1)
    {
        printf ("%d", iter->key);
        if (iter->next == head->prev || iter->next == NULL)
            return;
        iter = iter->next;
    }
}

/*
 * Insert a *new_node* at the sorted position so that the keys of the nodes of
 * the
 * list (including the key of the *new_node*) is always sorted (increasing
 * order)
 */
void
insert_sorted_by_key (list_node *head, list_node *new_node)
{
    if (is_list_empty (head))
    {
        insert_node_after (head, new_node);
        return;
    }

    list_node *iter = head->next;

    while (new_node->key > iter->key)
    {
        if (iter == (head->prev))
        {
            insert_node_after (iter->prev, new_node);
            return;
        }
        iter = iter->next;
    }

    insert_node_after (iter->prev, new_node);
    return;
}
