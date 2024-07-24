set(LLVM_LINK_COMPONENTS Support)
add_clang_tool(thebesttv
    main.cpp
    utils.cpp
    GenICFG.cpp
    ICFG.cpp
    CompilationDatabase.cpp
    GenAST.cpp
    DumpPath.cpp
    matcher/base.cpp
    matcher/npe.cpp
)

# add debug
# set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

# enable exceptions
target_compile_options(thebesttv PRIVATE -fexceptions)

# 使用 fmt 11.0.2
include(FetchContent)
FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt
  GIT_TAG        11.0.2)
FetchContent_MakeAvailable(fmt)
# 或者也可以解压源码到目录中
# add_subdirectory(fmt-11.0.2)

target_link_libraries(thebesttv
    PRIVATE
    clangAST
    clangBasic
    clangDriver
    clangFrontend
    clangRewriteFrontend
    clangSerialization
    clangStaticAnalyzerFrontend
    clangTooling
    clangToolingSyntax
    fmt::fmt
)
