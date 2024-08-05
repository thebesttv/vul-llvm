#include "stdio.h"
int g_table;

#define IS_ID_VALID(expr, errcode)               \
    do {                                         \
        if ((expr)) {                            \
            return (errcode);                    \
        }                                        \
    } while (0)
    
int *func2(int id);

int func1(int id)
{
    int *ptr = func2(id);
    IS_ID_VALID(ptr != NULL, 1);

    return (*ptr);
}
