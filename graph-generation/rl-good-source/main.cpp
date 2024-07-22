#include <cstdlib>

class A {
    int data;

  public:
    explicit A(int n) : data(n) {}
};

A *foo() { return (A *)malloc(sizeof(A)); } // 在 input.json 中
A *bar() { return new A(10); }              // 没有在 input.json 中指定

int main(int argc, char *argv[]) {
    void *a1 = malloc(sizeof(A));
    A *a2 = (A *)malloc(sizeof(A));
    A *a3 = (A *)(A *)(A *)malloc(sizeof(A));
    A *a4 = foo();
    A *a5 = bar();

    void *b1 = (void *)new A(argc);
    A *b2 = new A(argc);

    a1 = (A *)malloc(sizeof(A));
    a4 = foo();
    a5 = bar();

    return 0;
}
