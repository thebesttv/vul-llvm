#include <stdio.h>

template <typename T>
class TypeTest {
public:
	static T *GetPtr()
	{
		if (buffer_ == nullptr) {
			return nullptr;
		}
		return buffer_;
	}
	int size()
    {
        return int(end_ - buffer_); // sink1 col 9 var:this
    }
	
	void usr_ptr(TypeTest *input)
	{
		printf("%d", input->size()); // sink2 col 22
	}
protected:
	static T *buffer_;
    T *end_;
};

class TypeTest_2 : public TypeTest<TypeTest_2> {
public:
    int mocData_;
};

// 8358680908639030496
int test1()
{
	return TypeTest_2::GetPtr()->mocData_;
}

// 8358680908639030498
int test2()
{
	TypeTest_2 *ptr = TypeTest_2::GetPtr();
	return ptr->mocData_;
}
