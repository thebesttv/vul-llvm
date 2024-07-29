#include <stdio.h>

// return null的下一个节点非赋空语句
int g_table;
int *GetPtr(int a)
{
    if (a > 0) {
		return NULL; // src
	}
	return &g_table;
}

int *GetPtr1(int a)
{
    return GetPtr(a);
}

int *GetPtr2(int a)
{
    return GetPtr1(a);
}

int DefPtr(int a)
{
    int *ptr = GetPtr2(a);
	return *ptr; // sink
}

int DefPtr2(int a)
{
    int *ptr = GetPtr(a);
	return *ptr; // sink
}


// 指针判空语句位于三元运算符中
int func(int *ptr, int a)
{
    int flag = (ptr != NULL) ? 0 : 1; // src
	if (a > 1) {
		return flag;
	}
	return *ptr; // sink
}


int func2(int *ptr, int a)
{
    int flag = (ptr == NULL) ? 0 : 1; // src
	if (a > 1) {
		return flag;
	}
	return *ptr; // sink
}

int func3(int *ptr, int a)
{
    int flag = (ptr != NULL) ? 0 : 1; // npe-good-source
	if (flag == 1) {
		return flag;
	}
	return *ptr;
}

int func4(int *ptr, int a)
{
    int flag = (ptr == NULL) ? 0 : 1; // npe-good-source
	if (flag == 0) {
		return flag;
	}
	return *ptr;
}

int func5(int a)
{
	int *ptr = 
		GetPtr(a);
	return *ptr;
}
