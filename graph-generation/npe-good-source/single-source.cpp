#include <cstdlib>

int *nonNull() { return new int; }

int *null() { return nullptr; }

int *mayNull() {
    if (rand() % 2 == 0) {
        return new int;
    } else {
        return nullptr;
    }
}

int singleSourceMain() {
    // 形如 p = null，会包括
    int *x1 = 0;
    int *x2 = NULL;
    int *x3 = nullptr;

    // 不会包括
    int *y1 = (int *)malloc(sizeof(int));
    int *y2 = new int;
    int *y3 = x1;
    int *y4 = y1;

    // 形如 p = foo()，会包括 foo() 中有 return NULL 的
    int *z1 = nonNull();
    int *z2 = null();
    int *z3 = mayNull();

    // 加入 cast 的情况
    int *c1 = (int *)nonNull();
    int *c2 = (int *)(int *)null();
    int *c3 = (int *)(int *)(int *)mayNull();

    x1 = (int *)0;
    x2 = (int *)(int *)NULL;
    x3 = (int *)(int *)(int *)nullptr;

    return 0;
}
