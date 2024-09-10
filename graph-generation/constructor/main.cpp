class Node {
  public:
    int *p;

    Node(int x) {
        if (x)
            p = nullptr;
    }

    Node(int x, int y) {
        if (x + y)
            p = nullptr;
    }
};

int main() {
    int y;

    Node x1(1);
    y = *x1.p;

    Node x2(1, 2);
    y = *x2.p;
}
