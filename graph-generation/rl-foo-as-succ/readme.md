在 Infer 的 MEMORY_LEAK_C 报告中，可能出现以下情况：
- 中间路径中包括 `zend_llist_add_element()` 内部的语句 (L39)
- sink 是 `zend_llist_add_element()` 这个函数调用

在匹配 `foo()` 时，由于 `foo()` 只会在 pred 中出现，而 succ 是没有的，
所以之前的处理，就是[直接匹配失败，跳过这条语句](https://github.com/thebesttv/vul-llvm/blob/cefc22adccff3619642df0b9e77cb818fd7102e4/clang/tools/thebesttv/main.cpp#L251)。

然而，如果此时恰好 `foo()` 又是 sink，就会出问题。

因此，修改代码，寻找 `foo()` 的后继 CFGBlock。
