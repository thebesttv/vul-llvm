// 测试形如 p != NULL 的 NPE good source 的生成
#include <cstdlib>

int main() {
    int *p;
    int i;

    // 形如 p == NULL
    if (p == NULL)
        ;
    if (NULL == p)
        ;

    // 形如 p != NULL
    if (p != NULL)
        ;
    if (p != nullptr)
        ;
    if (p != 0)
        ;
    if (NULL != p)
        ;
    if (nullptr != p)
        ;
    if (0 != p)
        ;

    i = p != NULL ? 10 : 20;

    // 两边均为 NULL
    if (NULL == NULL)
        ;
    if (NULL != NULL)
        ;

    // 非指针
    if (i != 0)
        ;
    if (0 != i)
        ;
}
