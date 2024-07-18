#include <iostream>
#include <vector>
using namespace std;

int main(int argc, char *argv[]) {
    std::vector<int> v;
    for (int i = 1; i < argc; i++) {
        v.push_back(atoi(argv[i]));
    }

    int sum = 0;
    for (int num : v) {
        std::cout << "> " << num << std::endl;
        sum += num;
    }
    std::cout << "sum: " << sum << std::endl;

    return 0;
}
