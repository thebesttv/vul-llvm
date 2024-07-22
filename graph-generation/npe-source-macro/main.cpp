#define mayNull(x) ((x > 0) ? nullptr : &x)

int *f1(int n) {
    int *p = mayNull(n);
    return p;
}

#define foo f1

int f2(int n) {
    int *p = foo(n);
    return *p;
}
