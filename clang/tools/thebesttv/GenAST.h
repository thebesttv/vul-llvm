#pragma once

#include "CompilationDatabase.h"
#include "utils.h"

/**
 * 从 AST Dump 读取的 AST，可能为空。
 * 析构时会删除对应的 AST dump 文件。
 */
class ASTFromFile {
  private:
    std::string ASTDumpFile;
    std::shared_ptr<ASTUnit> AST;

  public:
    /**
     * 有两种方式读取 AST：
     * 1. 使用 ClangTool::buildASTs 直接读取。
     *    这种方法可以获得模板函数的实例化 body，
     *    但在一些项目中读取时，程序可能直接崩溃。
     * 2. 先用 clang -emit-ast 生成 AST dump 文件，
     *    然后用 ASTUnit::LoadFromASTFile 从 dump 中读取 AST。
     *    这种方法不会导致程序崩溃，但无法获得模板函数的实例化 body。
     */
    ASTFromFile(const std::string &file, bool fromASTDump);

    ~ASTFromFile() {
        if (!Global.keepAST && !ASTDumpFile.empty()) {
            // 删除对应的 AST dump 文件
            llvm::sys::fs::remove(ASTDumpFile);
        }
    }

    // 可能为空
    std::shared_ptr<ASTUnit> getAST() { return AST; }
};

std::string getASTDumpFile(const std::string &file);

/**
 * 获取 CompilationDatabase，并且对每条命令，修改为生成 AST dump
 */
std::unique_ptr<CompilationDatabase>
getCompilationDatabaseWithASTEmit(fs::path buildPath);

/**
 * 调用 clang 生成 AST dump
 */
int generateASTDump(const CompileCommand &cmd);

std::shared_ptr<ASTFromFile> getASTOfFile(std::string file);
