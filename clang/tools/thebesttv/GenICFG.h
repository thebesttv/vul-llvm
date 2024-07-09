#pragma once

#include "utils.h"
#include "clang/AST/Mangle.h"
#include "clang/Frontend/CompilerInstance.h"
#include "llvm/Demangle/Demangle.h"

class GenICFGVisitor : public RecursiveASTVisitor<GenICFGVisitor> {

    ASTContext *Context;
    fs::path filePath;

    /**
     * 获取 mangling 后的函数名
     *
     * 见 https://stackoverflow.com/q/40740604
     */
    std::string getMangledName(const FunctionDecl *decl);

    /**
     * 通过 Clang 的 mangling 和 llvm 的 demangling 获取函数的完整签名
     *
     * 目前不用，改用 utils.h 的 getFullSignature()
     */
    std::string getFullSignatureThroughMangling(const FunctionDecl *D) {
        return llvm::demangle(getMangledName(D));
    }

  public:
    explicit GenICFGVisitor(ASTContext *Context, fs::path filePath)
        : Context(Context), filePath(filePath) {}

    bool VisitFunctionDecl(clang::FunctionDecl *D);
    bool VisitCXXRecordDecl(clang::CXXRecordDecl *D);
};

class NpeSourceVisitor : public RecursiveASTVisitor<NpeSourceVisitor> {

    ASTContext *Context;
    int fid; // 当前访问函数的 fid

    bool isNullPointerConstant(const Expr *expr);
    const FunctionDecl *getDirectCallee(const Expr *expr);

    std::optional<typename std::set<ordered_json>::iterator>
    saveNpeSuspectedSources(const SourceRange &range);

    /**
     * source 的保存策略：
     * 1. p = NULL
     * 2. p = foo() && foo() = { ...; return NULL; }
     *    其中第二个判断（foo() 中包含 return NULL）在之后才会做。
     *    目前放到 main() 里做，在 generateICFG() 之后
     */
    void checkSourceAndMaybeSave(const SourceRange &range, Expr *rhs);

  public:
    explicit NpeSourceVisitor(ASTContext *Context, int fid)
        : Context(Context), fid(fid) {}

    bool VisitVarDecl(VarDecl *D);
    bool VisitBinaryOperator(BinaryOperator *S);
    bool VisitReturnStmt(ReturnStmt *S);
};

class GenICFGConsumer : public clang::ASTConsumer {
  public:
    explicit GenICFGConsumer(ASTContext *Context, fs::path filePath)
        : Visitor(Context, filePath) {}

    virtual void HandleTranslationUnit(clang::ASTContext &Context) override;

  private:
    GenICFGVisitor Visitor;
};

class GenICFGAction : public clang::ASTFrontendAction {
  public:
    virtual std::unique_ptr<clang::ASTConsumer>
    CreateASTConsumer(clang::CompilerInstance &Compiler,
                      llvm::StringRef InFile) override;
};

bool updateICFGWithASTDump(const std::string &file);

/**
 * 生成全程序调用图
 */
void generateICFG(const CompilationDatabase &cb);
void generateICFGParallel(const CompilationDatabase &cb, int numThreads = 0);
