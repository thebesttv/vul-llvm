class Base1 {
  public:
    int x;

    Base1(int n) {
        x = n;
        x++;
    }
};

class Base2 {
  public:
    int y;

    Base2(int n) {
        y = n;
        y++;
    }
};

class Derived : public Base1, public Base2 {
  public:
    int z;

    Derived(int n1, int n2, int n3) : Base1(n1), Base2(n2) {
        z = n3;
        z++;
    }
};

int mainMulti() {
    int x = 0;
    Derived d(10, 20, 30);
    int y = d.x + d.y + d.z;
    return 0;
}
