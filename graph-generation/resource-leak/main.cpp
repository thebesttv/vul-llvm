#include <cstdio>
#include <cstdlib>

struct Node {
    int data;
};

void panic() { exit(1); }

Node *createNode() {
    Node *node = (Node *)malloc(sizeof(Node));
    if (!node) {
        panic();
    }
    return node;
}

Node *createNode(int n) {
    Node *node = createNode();
    node->data = n;
    return node;
}

int main(int argc, char *argv[]) {
    Node *node = createNode(1);
    if (argc == 2) {
        int x = node->data + 10;
    }
    return 0;
}
