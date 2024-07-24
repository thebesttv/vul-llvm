#include <vector>

struct Node {
    int x, y;
    Node(int x, int y) : x(x), y(y) {}
    bool operator==(const Node &rhs) const { return x == rhs.x && y == rhs.y; }
    bool operator<(const Node &rhs) const {
        return x < rhs.x || (x == rhs.x && y < rhs.y);
    }
};

template <typename T> T minimum(const T &lhs, const T &rhs) {
    bool result = lhs < rhs;
    if (result) {
        return lhs;
    } else {
        return rhs;
    }
}

template <typename T> int test_template(T &dataList) {
    int x = 10;
    for (auto &item : dataList) {
        *item;
    }
    return x;
}

void foo() {}

int main(int argc, char *argv[]) {
    {
        int a = 1, b = 2;
        auto c = minimum(a, b);
        foo();
        int d = c;
    }

    {
        Node a(1, 2), b(3, 4);
        auto c = minimum<Node>(a, b);
        foo();
        auto d = c;
    }

    {
        std::vector<int *> v;
        int x = test_template(v);
        x = 20;
    }

    return 0;
}
