#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXPECTED(condition) (condition)
#define pemalloc(size, persistent)                                             \
    ((persistent) ? __zend_malloc(size) : __zend_malloc(size))

typedef unsigned long int uintptr_t;
typedef unsigned char byte;

typedef struct _zend_llist_element {
    struct _zend_llist_element *next;
    struct _zend_llist_element *prev;
    char data[1]; /* Needs to always be last in the struct */
} zend_llist_element;

typedef struct _zend_llist {
    zend_llist_element *head;
    zend_llist_element *tail;
    size_t count;
    size_t size;
    unsigned char persistent;
    zend_llist_element *traverse_ptr;
} zend_llist;

void panic() { exit(1); }

void *__zend_malloc(size_t len) {
    void *tmp = malloc(len);
    if (tmp == NULL) {
        panic();
    }
    return tmp;
}

void zend_llist_add_element(zend_llist *l, const void *element) {
    zend_llist_element *tmp =
        pemalloc(sizeof(zend_llist_element) + l->size - 1, l->persistent);

    tmp->prev = l->tail;
    tmp->next = NULL;
    if (l->tail) {
        l->tail->next = tmp;
    } else {
        l->head = tmp;
    }
    l->tail = tmp;
    memcpy(tmp->data, element, l->size);

    ++l->count;
}

int main(int argc, char *argv[]) {
    zend_llist *list = pemalloc(sizeof(zend_llist), 0);
    char *buffer = "lalala";
    zend_llist_add_element(list, &buffer);
    return 0;
}
