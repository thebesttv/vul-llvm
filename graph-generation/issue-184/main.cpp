#include <stdio.h>

int func2(int *ptr, int a)
{
    int flag;
	flag = (ptr == NULL) ? 0 : 1; // src
	if (a > 1) {
		return flag;
	}
	return *ptr; // sink
}
