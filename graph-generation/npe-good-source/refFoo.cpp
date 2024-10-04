struct A {
    int x;
    int (*g)();
};

int *nullInt() { return nullptr; }

int *nonNullInt() { return new int; }

A *nullA() { return nullptr; }

A *nonNullA() { return new A; }

int refFooMain() {
    // 对 foo() 解引用，包括 *foo()、foo()[i]、foo()->x
    int x1 = *nullInt();
    int x2 = nullInt()[x1];
    int x3 = nullA()->x;
    // 目前 source 语句会缺少 g 后面的括号，不过不是大问题
    int x4 = nullA()->g();

    int y1 = ((A *)nullInt())->x;
    // 同样，缺少 g 的括号
    int y2 = ((A *)nullInt())->g();

    // 对应的 nonNull 版本，不应该出现在 output 中
    int nx1 = *nonNullInt();
    int nx2 = nonNullInt()[x1];
    int nx3 = nonNullA()->x;
    int nx4 = nonNullA()->g();

    int ny1 = ((A *)nonNullInt())->x;
    int ny2 = ((A *)nonNullInt())->g();

    return 0;
}
