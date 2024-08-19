#include <stdio.h>

// 指针返回直接作为函数入参
int g_table;
int *GetPtr(int a)
{
	if (a > 1) {
		return NULL;
	}
	return &g_table;
}

int DefPtr(int a, int *ptr) 
{
	if (a > 1) {
		return a;
	}
	return *ptr;
}

int DefPtr2(int a)
{
	int tmp = DefPtr(a, GetPtr(a));
	return tmp;
} 

int DefPtr3(int a)
{
	int tmp;
	tmp = DefPtr(a, GetPtr(a));
	return tmp;
} 

int DefPtr4(int a)
{
	return DefPtr(a, GetPtr(a));
} 
