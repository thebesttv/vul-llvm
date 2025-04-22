#include <array>
#include <cstdlib>
#include <string>
#include <vector>
using namespace std;

int main() {
    int a1[10];
    int a2[10] = {1, 2, 3};
    std::array<int, 10> a3;
    array<int, 10> a4 = {1, 2, 3};

    std::vector<int> v1;
    vector<int> v2 = {1, 2, 3};
    std::string s1;
    string s2("123");

    int *p1 = new int[10];
    int *p2 = new int[10]{1, 2, 3};
    int *p3 = (int *)malloc(10 * sizeof(int));
}
