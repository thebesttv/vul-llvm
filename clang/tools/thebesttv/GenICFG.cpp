#include "GenICFG.h"
#include "DumpPath.h"
#include "GenAST.h"
#include "ICFG.h"
#include "matcher/doubleFree.h"
#include "matcher/npe.h"
#include "matcher/resourceLeak.h"
#include "utils.h"

#include "lib/BS_thread_pool.hpp"

std::string GenICFGVisitor::getMangledName(const FunctionDecl *decl) {
    auto mangleContext = Context->createMangleContext();

    if (!mangleContext->shouldMangleDeclName(decl)) {
        return decl->getNameInfo().getName().getAsString();
    }

    std::string mangledName;
    llvm::raw_string_ostream ostream(mangledName);

    mangleContext->mangleName(decl, ostream);

    ostream.flush();

    delete mangleContext;

    return mangledName;
};

bool GenICFGVisitor::VisitFunctionDecl(FunctionDecl *D) {
    std::unique_ptr<Location> pLoc =
        Location::fromSourceLocation(D->getASTContext(), D->getBeginLoc());
    if (!pLoc)
        return true;

    // 记录 .h 文件的 AST 对应 cc.json 中哪个文件
    auto &sourceForFile = Global.icfg.sourceForFile;
    if (sourceForFile.find(pLoc->file) == sourceForFile.end()) {
        sourceForFile[pLoc->file] = filePath;
    }

    /**
     * 跳过不在 Global.projectDirectory 中的函数。
     *
     * See: Is it a good practice to place C++ definitions in header files?
     *      https://stackoverflow.com/a/583271
     */
    if (!Global.isUnderProject(pLoc->file))
        return true;

    if (!D->isThisDeclarationADefinition())
        return true;

    if (!D->getBody())
        return true;

    std::string fullSignature = getFullSignature(D);

    auto &functionCnt = Global.functionCnt;
    auto &fileAndIdOfFunction = Global.fileAndIdOfFunction;
    auto &functionLocations = Global.functionLocations;

    // declaration already processed
    auto it = fileAndIdOfFunction.find(fullSignature);
    if (it != fileAndIdOfFunction.end()) { // 首先看函数签名是否已经见过
        auto &fidOfFile = it->second;
        // 然后判断函数定义所在的文件是不是也见过了
        if (fidOfFile.find(pLoc->file) != fidOfFile.end()) {
            // logger.warn("Skipping func {} in {}:{}:{}", fullSignature,
            //             pLoc->file, pLoc->line, pLoc->column);
            return true;
        }
    }

    /**
     * 对于模板函数，有两种处理方法：
     * 1. 直接把模板函数加入 ICFG 中，不考虑 template 实例化。
     *    这需要把 ICFG 中的函数调用重定向到原始的模板函数，而不是实例化后的。
     * 2. 把模板函数的所有实例化结果加入 ICFG 中。
     *    但这样在 input.json 进行匹配的时候，无法确定是哪个实例化后的函数。
     * 所以暂时采用第一种方法。
     */
    if (D->isTemplated()) {
        logger.warn("Loading template function: {}", fullSignature);
    }

    std::unique_ptr<CFG> cfg = buildCFG(D);
    if (!cfg) {
        logger.error("Unable to create CFG for function {} in {}:{}:{}",
                     fullSignature, pLoc->file, pLoc->line, pLoc->column);
        return true;
    }

    fileAndIdOfFunction[fullSignature][pLoc->file] = functionCnt++;
    functionLocations.emplace_back(*pLoc, fullSignature);

    // 记录函数结束位置，如果无法获得，就 fallback 成开始位置
    std::unique_ptr<Location> eLoc =
        Location::fromSourceLocation(D->getASTContext(), D->getEndLoc());
    Global.functionEndLocations.push_back(eLoc ? *eLoc : *pLoc);

    CallGraph CG;
    CG.addToCallGraph(D);
    // CG.dump();

    CallGraphNode *N = CG.getNode(D->getCanonicalDecl());
    if (N == nullptr) {
        /**
         * 不知道为什么，CG还是有可能只有一个root节点。
         *
         * 样例 linux，版本 6.7.5.arch1-1
         * linux/src/linux-6.7.5/include/linux/bsearch.h 的函数
         * __inline_bsearch()
         */
        requireTrue(CG.size() == 1, "Empty call graph! (only root node)");
    } else {
        for (CallGraphNode::const_iterator CI = N->begin(), CE = N->end();
             CI != CE; ++CI) {
            FunctionDecl *callee = CI->Callee->getDecl()->getAsFunction();
            requireTrue(callee != nullptr, "callee is null!");
            Global.callGraph[fullSignature].insert(getFullSignature(callee));
        }
    }

    int fid = Global.getIdOfFunction(fullSignature, pLoc->file);
    Global.icfg.addFunction(fid, *cfg);

    NpeGoodSourceVisitor(Context, fid).TraverseDecl(D);
    ResourceLeakGoodSourceVisitor(Context, fid).TraverseDecl(D);
    DoubleFreeGoodSourceVisitor(Context, fid).TraverseDecl(D);

    /*
    // traverse CFGBlocks
    for (auto BI = cfg->begin(); BI != cfg->end(); ++BI) {
        CFGBlock &B = **BI;
        // traverse all stmts
        for (auto SI = B.begin(); SI != B.end(); ++SI) {
            CFGElement &E = *SI;
            if (std::optional<CFGStmt> CS = E.getAs<CFGStmt>()) {
                const Stmt *S = CS->getStmt();
                dumpStmt(*Context, S);
                for (const Stmt *child : S->children()) {
                    if (child) {
                        dumpStmt(*Context, child);
                    }
                }
            }
        }
    }
    */

    return true;
}

bool GenICFGVisitor::VisitCXXRecordDecl(CXXRecordDecl *D) {
    // llvm::errs() << D->getQualifiedNameAsString() << "\n";
    return true;
}

void GenICFGConsumer::HandleTranslationUnit(clang::ASTContext &Context) {
    TranslationUnitDecl *TUD = Context.getTranslationUnitDecl();
    // TUD->dump();
    // CallGraph CG;
    // CG.addToCallGraph(TUD);
    // CG.dump();
    Visitor.TraverseDecl(TUD);
}

std::unique_ptr<clang::ASTConsumer>
GenICFGAction::CreateASTConsumer(clang::CompilerInstance &Compiler,
                                 llvm::StringRef InFile) {

    static const int total = Global.cb->getAllCompileCommands().size();
    static int cnt = 0;
    cnt++;

    SourceManager &sm = Compiler.getSourceManager();
    const FileEntry *fileEntry = sm.getFileEntryForID(sm.getMainFileID());
    std::string filePath(fileEntry->tryGetRealPathName());
    logger.info("[{}/{}] {}", cnt, total, filePath);

    return std::make_unique<GenICFGConsumer>(&Compiler.getASTContext(),
                                             filePath);
}

std::mutex icfgMtx;
bool updateICFGWithASTDump(const std::string &file) {
    auto ASTFromFile = getASTOfFile(file);
    auto AST = ASTFromFile->getAST();
    if (AST) {
        icfgMtx.lock();
        auto &Context = AST->getASTContext();
        GenICFGVisitor visitor(&Context, file);
        visitor.TraverseDecl(Context.getTranslationUnitDecl());
        icfgMtx.unlock();
        return true;
    }
    return false;
}

void generateICFG(const CompilationDatabase &cb) {
    logger.info("--- Generating whole program call graph ---");

    auto allCmds = cb.getAllCompileCommands();
    ProgressBar bar("Gen ICFG", allCmds.size());
    int badCnt = 0, goodCnt = 0;
    for (auto &cmd : allCmds) {
        // 如果 keepAST 为 true，那很可能磁盘上已经有上一次的 AST dump 了
        //     就算没有，getASTOfFile() 也会重新生成
        // 如果 keepAST 为 false，就保留之前的逻辑，显式调用 generateASTDump()
        int ret = Global.keepAST ? 0 : generateASTDump(cmd);
        if (ret == 0) {
            bool result = updateICFGWithASTDump(cmd.Filename);
            if (result == true) {
                goodCnt++;
            } else {
                badCnt++;
            }
        } else {
            badCnt++;
        }
        bar.tick();
    }
    bar.done();

    logger.info("ICFG generation finished with {} success and {} failure",
                goodCnt, badCnt);
}

void generateICFGParallel(const CompilationDatabase &cb, int numThreads) {
    logger.info("--- Generating whole program call graph (parallel) ---");

    BS::thread_pool pool(numThreads);
    std::vector<std::future<int>> tasks;

    auto allCmds = cb.getAllCompileCommands();
    ProgressBar bar("Gen ICFG", allCmds.size());
    for (const auto &cmd : allCmds) {
        tasks.push_back(pool.submit_task([cmd, &bar] {
            int ret = generateASTDump(cmd);
            if (ret == 0) {
                bool result = updateICFGWithASTDump(cmd.Filename);
                requireTrue(result == true);
            }
            bar.tick();
            return ret;
        }));
    }

    int badCnt = 0, goodCnt = 0;
    for (auto &task : tasks) {
        int ret = task.get();
        ret == 0 ? goodCnt++ : badCnt++;
    }
    bar.done();

    logger.info("ICFG generation finished with {} success and {} failure",
                goodCnt, badCnt);
}
