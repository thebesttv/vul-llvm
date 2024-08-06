#include <stdio.h>

#define IS_NULL(x) ((x) == NULL)

void Test1(int *ptr)
{
    if (IS_NULL(ptr)) {
        printf("null ptr");
    }
  
    printf("%d", *ptr);
}

void Test2(int *ptr)
{
    if (IS_NULL(ptr)) {
        return;
    }
  
    printf("%d", *ptr);
}
