#include "A.h"

int n = 10;
A a(&n);

int mainUseA() {
    A *p = &a;

    // begin 在这里，但 end 在 A.h
    crossFile(p);
    n += *p->data;

    // begin & end 都在 A.h
    bothInHeader();
    n += *p->data;

    // begin & end 都在这里
    bothInSource(p->data);
    n += *p->data;

#define cross(p) p->data = nullptr

    // begin & end 在同一文件，但不同位置
    cross(p);
    n += *p->data;

    return n;
}
