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

void *GetTestAddr()
{
    return GetAddr(0);
}

#define MEM_STRAT_ADDR ((unsigned char *)GetTestAddr() + 0x20000)
#define LOG_START_ADDR (MEM_STRAT_ADDR + 0x30000)

// 288230376390080036 inf
int *test0()
{
    int *addr = (int *)MEM_STRAT_ADDR; // source点是GetTestAddr()返回值 sink点是宏内指针的偏移，input.json给到首列号，需讨论
    return addr;
}

// 288230376390080032 inf
void *test1()
{
    void *addr = (void *)LOG_START_ADDR; // source sink
    return addr;
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
        return (char *)ptr + 0x20000;
    }
    
    void ProcessPtr()
    {
        cout << ptr << endl;
    }
    
    private:
    void *ptr = nullptr;
    
};

#define GET_ADDR_INTERFACE GetCharAddr<TestClass>

int UseAddr(void *ptr);
int test2()
{
    // 8358680908639030505
    return UseAddr(GET_ADDR_INTERFACE()->GetPtr()); // sink点是指针指向成员函数的调用  source点是GetAddr的return null 语句，该场景source点识别是否需要后移，待讨论
}

void test3()
{
    // 8358680908639030507
    GET_ADDR_INTERFACE()->ProcessPtr(); // sink
}
