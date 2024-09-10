#include <stdio.h>

template <class T>
class TestClass1 {
public:
    TestClass1(T *data) : data_(data)
    {
        ptr = &data->ptr; // sink
    };
    
protected:
    T *data_;
    int **ptr;
};

template <class T>
class TestClass2 : public TestClass1<T> {
public:
    explicit TestClass2(const T *table) : TestClass1<T>(const_cast<T *>(table)){};
};

struct TestStru {
    int num;
    int *ptr;
};

TestStru *GetData();
void UsePtr2(const TestClass2<TestStru> &table);

void UsePtr(const TestStru &table)
{
    TestClass2<TestStru> table2(&table);
    UsePtr2(table2);
}

// 1441151880815120306
void test()
{
    TestStru *table = GetData(); // source
    UsePtr(*table);
}
