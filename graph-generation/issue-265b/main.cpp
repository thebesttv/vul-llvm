void usePtr(int *ptr1, int *ptr2)
{
    *ptr1 = *ptr2;
}

void test(int *ptr1)
{
    usePtr(ptr1, nullptr);
}

int *usePtr1(int *ptr1, int *ptr2)
{
	if (ptr1 == nullptr) {
		return nullptr;
	}
    *ptr1 = *ptr2;
	return ptr1;
}

int test1(int *ptr1)
{
    int *p = usePtr1(ptr1, nullptr);
	return *p;
}
