int *extNull();
int *extNonNull();
// 会在 input.json 中显式指定为 null
int *overrideNull() { return new int; }

/**
 *      f0
 *    /    \
 *   f1    f2
 *  / \   /  \
 * f3 f4 f5  f6
 * ^^    ^^
 */

int *f3(int n) { return extNull(); }
int *f4(int n) { return extNonNull(); }
int *f5(int n) { return overrideNull(); }
int *f6(int n) { return extNonNull(); }

int *f1(int n) {
    if (n < -10)
        return f3(n);
    else
        return f4(n);
}
int *f2(int n) {
    if (n < 10)
        return f5(n);
    else
        return f6(n);
}

int *f0(int n) {
    if (n < 0)
        return f1(n);
    else
        return f2(n);
}

int chainReturnMain() {
    int *p, n;

    p = f0(n);
    p = f1(n);
    p = f2(n);
    p = f3(n);
    p = f4(n);
    p = f5(n);
    p = f6(n);

    return 0;
}
