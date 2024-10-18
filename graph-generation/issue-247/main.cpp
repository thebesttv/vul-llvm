#include <iostream>
#include <cstdint>
using namespace std;
#define ERROR_CODE() __catchErrorCode
#define ERROR_LINE_NO() __catchErrorLineNo
#define ERROR_PROC()

#define PROG                                    \
    uint32_t __catchErrorCode;   \
    uint32_t __catchErrorLineNo; \
    {

#define END_PROG            \
    }                       \
    __tabErrorCode:

#define THROW(errcode) do {                         \
    __catchErrorCode = (errcode);                   \
    __catchErrorLineNo = __LINE__;                  \
    goto __tabErrorCode;                            \
} while (0)

#define EXEC(func)                                    \
    {                                                 \
        if (0 != (__catchErrorCode = (func))) {  \
            THROW(__catchErrorCode);                  \
        }                                             \
    }

#define JUDGE(errcode, expr) \
    {                        \
        if (!(expr)) {       \
            THROW(errcode);   \
        }                    \
    }


#define CATCH_ALL_ERROR \
    {                   \
    ((void)(__catchErrorLineNo + __catchErrorCode));

#define END_CATCH_ERROR }



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
    
    int ProcessPtr()
    {
        cout << ptr << endl; // sink
        return 0;
    }
    
    private:
    void *ptr = nullptr;
    
};

#define GET_ADDR_INTERFACE GetCharAddr<TestClass>

int UseAddr(void *ptr);
int test2()
{
PROG
    EXEC(UseAddr(GET_ADDR_INTERFACE()->GetPtr())); // source 是GET_ADDR_INTERFACE() sink line 66
    return 0;
END_PROG
CATCH_ALL_ERROR
    return ERROR_CODE();
END_CATCH_ERROR
}

int test3()
{
PROG
    EXEC(GET_ADDR_INTERFACE()->ProcessPtr()); // source 是GET_ADDR_INTERFACE() sink line 71
    return 0;
END_PROG
CATCH_ALL_ERROR
    return ERROR_CODE();
END_CATCH_ERROR
}

int test4()
{
PROG
    EXEC(GetCharAddr<TestClass>()->ProcessPtr()); // source 是GetCharAddr sink line 71
    return 0;
END_PROG
CATCH_ALL_ERROR
    return ERROR_CODE();
END_CATCH_ERROR
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

int UsePtr(TestStruct *ptr)
{
    cout << ptr->a << endl;
    return 0;
}

int UsePtr2(int a)
{
    cout << a << endl;
    return 0;
}

int test0(int type)
{
PROG
    EXEC(UsePtr(GetPtr(0))); // source是GetPtr，sink是line132
    return 0;
END_PROG
CATCH_ALL_ERROR
    return ERROR_CODE();
END_CATCH_ERROR
}

int test1(int type)
{
PROG
    EXEC(UsePtr2(
        GetPtr(0)->a)); // source是GetPtr，sink是line156
    return 0;
END_PROG
CATCH_ALL_ERROR
    return ERROR_CODE();
END_CATCH_ERROR
}
