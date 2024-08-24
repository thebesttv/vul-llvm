#include <stdio.h>

template <typename T>
class TypeTest {
public:
	int size()
    {
        return int(end_ - buffer_); // sink1 col 9 var:this
    }
	
	void usr_ptr(TypeTest *input)
	{
		printf("%d", input->size()); // sink2 col 22
	}
protected:
	T *buffer_;
    T *end_;
};

TypeTest<int> *GetNewList();
void test1()
{
	TypeTest<int> *newList = GetNewList(); // source col 20
	printf("%d", newList->size());
}

void test2()
{
	TypeTest<int> *newList = GetNewList();
	TypeTest<int> *newList2 = GetNewList(); // source col 20
	newList->usr_ptr(newList2);
}
