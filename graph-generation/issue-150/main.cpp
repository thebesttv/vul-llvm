#include <iostream>
#include <vector>
class Test {
    private:
        std::vector<int> v1;
        int table;
    public:
        int *GetPtr(int input);
        void DefPtr(int input);
};

int *Test::GetPtr(int input)
{
    for (int num : v1) {
        if (num == input) {
            return &table;
        }
    }
    return nullptr; // source
}

void Test::DefPtr(int input)
{
    int *ptr = GetPtr(input);
    std::cout << *ptr << std::endl; // sink
}

// test template func cfg generate
template<typename T>
void test_template(T &dataList)
{
    for (auto &item : dataList) {
        std::cout << *item << std::endl; // sink
    }
}

template<typename T>
void test_template2(T &item)
{
    std::cout << *item << std::endl; // sink
}

void test_entry()
{
    std::vector<int *> data1;
    int *data2;
    data1.push_back(nullptr); // source
    data2 = nullptr; // source
    test_template(data1);
    test_template2(data2);
}

