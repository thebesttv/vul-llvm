#include <iostream>
using namespace std;

// 函数调用返回值用作指针偏移
int g_var = 0;
void *GetAddr(int type)
{
    if (type == 0) {
        return nullptr; // source
    }
    return &g_var;
}

template<typename Service>
Service *GetCharAddr()
{
    return static_cast<Service *>(GetAddr(0));
}

class TestClass {
    public:
    void *GetPtr()
    {
        return (char *)ptr + 0x20000; // sink
    }
    
    void ProcessPtr()
    {
        cout << ptr << endl; // sink
    }
    
    private:
    void *ptr = nullptr;
    
};

#define GET_ADDR_INTERFACE GetCharAddr<TestClass>

int UseAddr(void *ptr);
int test2()
{
    // 8358680908639030505
    return UseAddr(GET_ADDR_INTERFACE()->GetPtr()); // sink点是line 24 预期函数列表为 test2/GetPtr
}

void test3()
{
    // 8358680908639030507
    GET_ADDR_INTERFACE()->ProcessPtr(); // sink点是line 29 预期函数列表为 test3/ProcessPtr
}

void test4()
{
    GetCharAddr<TestClass>()->ProcessPtr(); // sink点是line 29 预期函数列表为 test3/ProcessPtr
}

typedef struct {
    int a;
    int b;
} TestStruct;

TestStruct g_atruct_var = {0};
TestStruct *GetPtr(int type)
{
    if (type == 0) {
        return nullptr; // source
    }
    return &g_atruct_var;
}

void UsePtr(TestStruct *ptr)
{
    cout << ptr->a << endl;
}

void UsePtr2(int a)
{
    cout << a << endl;
}

void test0(int type)
{
    UsePtr(GetPtr(0)); // source是GetPtr，sink是line73 预期函数列表：test0/UsePtr
}

void test1(int type)
{
    UsePtr2(GetPtr(0)->a); // source是GetPtr，sink是line88 预期函数列表：test0/UsePtr2
}
