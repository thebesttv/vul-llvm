#include "stdio.h"
int g_table;

#define IS_ID_VALID(expr, errcode)               \
    do {                                         \
        if ((expr)) {                            \
            return (errcode);                    \
        }                                        \
    } while (0)

int *func2(int id)
{
    IS_ID_VALID(id >= 2, NULL); // source
    return (int *)&g_table;
}

int func1(int id)
{
    int *ptr = func2(id);

    return (*ptr);
}
