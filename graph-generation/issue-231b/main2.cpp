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

struct TestStru;
class TestClass2 : public TestClass1<TestStru> {
public:
    explicit TestClass2(const TestStru *table) : 
		TestClass1<TestStru>(const_cast<TestStru *>(table)){};
};

struct TestStru {
    int num;
    int *ptr;
};

TestStru *GetData();
void UsePtr2(const TestClass2 &table);

void UsePtr(const TestStru &table)
{
    TestClass2 table2(&table);
    UsePtr2(table2);
}

// 1441151880815120306
void test()
{
    TestStru *table = GetData(); // source
    UsePtr(*table);
}
