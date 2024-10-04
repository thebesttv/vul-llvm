int *extNonNull();

int *extNull();

int *extMayNull();

class A {
  public:
    int *oNonNull();
    int *oNull();
    int *oMayNull();

    static int *sNonNull();
    static int *sNull();
    static int *sMayNull();

    class B {
      public:
        int *iNonNull();
        int *iNull();
        int *iMayNull();
    };
    B *b;
};

int externalMain() {
    int *x1 = extNonNull();
    int *x2 = extNull();
    int *x3 = extMayNull();

    A a;
    int *o1 = a.oNonNull();
    int *o2 = a.oNull();
    int *o3 = a.oMayNull();

    int *s1 = A::sNonNull();
    int *s2 = A::sNull();
    int *s3 = A::sMayNull();

    int *i1 = a.b->iNonNull();
    int *i2 = a.b->iNull();
    int *i3 = a.b->iMayNull();

    return 0;
}
