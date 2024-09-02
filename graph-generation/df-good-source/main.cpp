#include <cstdlib>

int *foo() { return (int *)malloc(sizeof(int)); }
void bar(void *p) { /* free(p); */ }

int main(int argc, char *argv[]) {
    int *a1 = foo();
    free(a1);

    int *a2 = foo();
    bar(a2);

    // bad-source
    int *a3 = foo();
    free(a3);

    int *b1 = new int;
    delete b1;

    int *b2 = new int[10];
    delete[] b2;

    // bad-source
    int *b3 = new int;
    delete b3;

    return 0;
}
