#include "CompilationDatabase.h"
#include "DumpPath.h"
#include "FunctionInfo.h"
#include "GenAST.h"
#include "GenICFG.h"
#include "ICFG.h"
#include "PathFinder.h"
#include "VarFinder.h"
#include "VarLocResult.h"
#include "matcher/npe.h"
#include "utils.h"
#include <fstream>
#include <iostream>
#include <unistd.h>

#include "lib/args.hxx"
#include <fmt/color.h>

#include "clang/Basic/Version.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/PrettyStackTrace.h"

class FunctionAccumulator : public RecursiveASTVisitor<FunctionAccumulator> {
  private:
    fif &functionsInFile;

  public:
    explicit FunctionAccumulator(fif &functionsInFile)
        : functionsInFile(functionsInFile) {}

    bool VisitFunctionDecl(FunctionDecl *D) {
        auto fi = FunctionInfo::fromDecl(D);
        if (fi == nullptr)
            return true;

        functionsInFile[fi->file].insert(std::move(fi));

        return true;
    }
};

void fixCompilationDatabase(fs::path path) {
    std::ifstream ifs(path);
    ordered_json input = ordered_json::parse(ifs);
    ifs.close();

    bool needsUpdate = false;
    std::set<std::string> visitedFiles;
    ordered_json output;
    for (auto &cmd : input) {
        std::string file = cmd["file"];
        if (visitedFiles.find(file) != visitedFiles.end()) {
            logger.trace("Duplicate entry for file: {}", file);
            needsUpdate = true;
            continue;
        }

        std::string dir = cmd["directory"];
        if (!dirExists(dir)) {
            logger.warn("Directory does not exist: {}", dir);
            needsUpdate = true;
            continue;
        }

        visitedFiles.insert(file);
        output.push_back(cmd);
    }

    if (!needsUpdate) {
        logger.info("No entry need fixing in compilation database");
        return;
    }

    logger.warn("Some entries need fixing in compilation database!");
    {
        fs::path backup = path.string() + ".bk";
        logger.warn("Original database is backed up to: {}", backup);
        std::ofstream o(backup);
        o << input.dump(4, ' ', false, json::error_handler_t::replace)
          << std::endl;
        o.close();
    }
    {
        std::ofstream o(path);
        o << output.dump(4, ' ', false, json::error_handler_t::replace)
          << std::endl;
        o.close();
        logger.warn("Fixsed database is in: {}", path);
    }
}

/**
 * isStmt: 匹配语句或变量（目前 source、stmt、sink 都当作语句处理）
 *
 * requireExact: 只对路径有效，表示是否需要精确匹配
 *
 * succFirst：在 CFG 中，对于语句 `p = foo()`，`foo()` 会在 pred，`p = foo()`
 * 会在 succ。如果想进入函数，需要优先访问 pred。对于 input.json 的 stmt
 * 类型，需要优先进入函数。但 source 和 sink 则不需要。
 * 但如果 source 是 p = foo() /
 * foo()，且下一条语句(nextFid)是那个被调用的函数中的， 那就会无视 succFirst。
 */
VarLocResult locateVariable(const fif &functionsInFile, const std::string &file,
                            int line, int column, bool isStmt,
                            bool requireExact, bool succFirst, int previousFid,
                            int nextFid, bool isNpeSource) {
    FindVarVisitor visitor;

    for (const auto &fi : functionsInFile.at(file)) {
        // function is defined later than targetLoc
        if (fi->line > line)
            continue;

        // search all CFG stmts in function for matching variable
        ASTContext *Context = &fi->D->getASTContext();
        const int fid = Global.getIdOfFunction(fi->signature, fi->file);

        auto locate = [&](const int bid,
                          const int sid) -> std::optional<VarLocResult> {
            const Stmt *stmt = fi->G[bid][sid];
            if (!stmt)
                return std::nullopt;
            if (isStmt) {
                // search for stmt
                auto range =
                    getProperSourceRange(*Context, stmt->getSourceRange());
                auto bLoc =
                    Location::fromSourceLocation(*Context, range.getBegin());
                auto eLoc =
                    Location::fromSourceLocation(*Context, range.getEnd());

                if (!bLoc || bLoc->file != file)
                    return std::nullopt;

                // 精确匹配：要求行号、列号相同
                bool matchExact = bLoc->line == line && bLoc->column == column;
                // 模糊匹配：行号在语句 begin 和 end 之间即可
                bool matchInexact =
                    eLoc != nullptr && bLoc->line <= line && line <= eLoc->line;

                if ((requireExact && matchExact) ||
                    (!requireExact && matchInexact)) {
                    auto result = VarLocResult(fid, bid, sid);
                    int nodeId =
                        Global.icfg
                            .nodeIdOfFunctionBlock[{result.fid, result.bid}];
                    logger.info("Found stmt in {} B{} ({}) at {}:{}:{}",
                                fi->signature, bid, nodeId, file, line, column);
                    return result;
                }
            } else {
                // search for var within stmt
                const std::string var =
                    visitor.findVarInStmt(Context, stmt, file, line, column);
                if (!var.empty()) {
                    logger.info("Found var '{}' in {} block {} at {}:{}:{}",
                                var, fi->signature, bid, file, line, column);
                    return VarLocResult(fid, bid, sid);
                }
            }
            return std::nullopt;
        };

        auto getFirstIntraProcSucc = [](VarLocResult loc) {
            int fid = loc.fid, bid = loc.bid;
            ICFG &icfg = Global.icfg;
            int u = icfg.getNodeId(fid, bid);
            for (const auto &e : icfg.G[u]) {
                if (e.type == ICFG::Edge::Type::INTRA_PROC) {
                    int v = e.target;
                    return VarLocResult(fid,
                                        icfg.functionBlockOfNodeId[v].second);
                }
            }
            return VarLocResult();
        };

        std::vector<VarLocResult> locResults;
        for (int bid = 0; bid < fi->n; bid++) {
            for (int sid = 0; sid < fi->G[bid].size(); sid++) {
                // 跳过 unreachable 的 BB
                if (!Global.icfg.reachable(fid, bid))
                    continue;
                auto result = locate(bid, sid);
                if (result) {
                    locResults.push_back(result.value());
                }
            }
        }

        if (isNpeSource) {
            locResults = NpeBugSourceVisitor(Context, fid)
                             .transform(locResults, fi->G, line, column);
        }

        if (locResults.empty())
            continue;

        // 如果函数调用比较复杂，会横跨多个 CFGBlock，其中 CallExpr 在几个 block
        // 的中间。从而导致 succ 优先无法匹配到 CallExpr。
        // 因此搜索所有匹配的语句，看有没有 CallExpr。
        // 复杂函数调用样例：zval *zv = xxx(... ? CG(...) : EG(...), lcname);
        for (const auto &r : locResults) {
            const Stmt *stmt = fi->G[r.bid][r.sid];

            const FunctionDecl *callee = nullptr;
            if (auto *expr = dyn_cast<CallExpr>(stmt)) {
                callee = getDirectCallee(expr);
            } else if (auto *expr = dyn_cast<CXXConstructExpr>(stmt)) {
                callee = getDirectCallee(expr);
            } else {
                continue;
            }

            // 匹配到 CallExpr，说明语句形如 p = foo() 或 foo() 等。
            // 此时位于 foo()。
            // 根据路径上下文，判断即将进入 foo() 还是已经退出 foo()。
            // - 如果即将进入，那定位到 foo() 即可，返回当前 BB
            // - 如果已经退出，那需要定位到返回后的 BB，默认是 ICFG 中下一个 BB

            std::string calleeSignature = getFullSignature(callee);
            int calleeFid = Global.getIdOfFunction(calleeSignature);
            if (calleeFid == -1) // 保证了 previousFid 和 nextFid 都不是 -1
                continue;

            int nodeId = Global.icfg.nodeIdOfFunctionBlock[{r.fid, r.bid}];
            logger.info("Found CallExpr {} in {} B{} ({}) at {}:{}:{}",
                        calleeSignature, fi->signature, r.bid, nodeId, file,
                        line, column);

            if (calleeFid == previousFid) {
                // 已经退出 foo()，要返回下一个 BB
                auto succ = getFirstIntraProcSucc(r);
                logger.info(
                    "  Just left call, returning next BB: B{} ({})", succ.bid,
                    Global.icfg.nodeIdOfFunctionBlock[{succ.fid, succ.bid}]);
                return succ;
            } else if (calleeFid == nextFid) {
                // 即将进入 foo()，返回当前 BB
                logger.info(
                    "  About to enter call, returning current BB: B{} ({})",
                    r.bid, nodeId);
                return r;
            }
        }

        // 分别获取，succ / pred 优先的匹配结果
        //   优先访问 succ，也就是根据 Block ID 从小到大遍历
        const VarLocResult &succFirstResult = locResults.front();
        //   优先访问 pred，即从大到小，反向遍历
        const VarLocResult &predFirstResult = locResults.back();

        if (succFirst) {
            return succFirstResult;
        } else {
            return predFirstResult;
        }
    }

    /**
     * Infer 有时会报告函数体末尾的 } 括号，导致无法匹配
     *
     * 这里在上面语句匹配失败后，试图匹配函数定义的结尾或开始。
     */
    if (isStmt && !requireExact) {
        for (const auto &fi : functionsInFile.at(file)) {
            // function is defined later than targetLoc
            if (fi->line > line)
                continue;

            // search all CFG stmts in function for matching variable
            ASTContext *Context = &fi->D->getASTContext();

            auto bLoc =
                Location::fromSourceLocation(*Context, fi->D->getBeginLoc());
            auto eLoc =
                Location::fromSourceLocation(*Context, fi->D->getEndLoc());

            // 如果结尾和开始在同一行，那照理之前语句能匹配到
            if (bLoc->line == eLoc->line)
                continue;

            // 优先匹配结尾
            if (eLoc->line == line) {
                logger.info("Found function exit in {} at {}:{}:{}",
                            fi->signature, file, line, column);
                return VarLocResult(fi, &fi->cfg->getExit());
            }
            // 然后匹配开始
            if (bLoc->line == line) {
                logger.info("Found function entry in {} at {}:{}:{}",
                            fi->signature, file, line, column);
                return VarLocResult(fi, &fi->cfg->getEntry());
            }
        }
    }

    return VarLocResult();
}

struct FunctionLocator {
    // file -> (fid, start line)
    std::map<std::string, std::vector<std::pair<int, int>>> functionLocations;

    FunctionLocator() {
        for (int i = 0; i < Global.functionLocations.size(); i++) {
            const Location &loc = Global.functionLocations[i];
            functionLocations[loc.file].emplace_back(i, loc.line);
        }
        // 根据 line 降序排列
        for (auto &[file, locs] : functionLocations) {
            std::sort(locs.begin(), locs.end(),
                      [](const auto &a, const auto &b) {
                          return a.second > b.second;
                      });
        }
    }

    int getFid(const Location &loc) const {
        auto it = functionLocations.find(loc.file);
        if (it == functionLocations.end())
            return -1;

        for (const auto &[fid, startLine] : it->second) {
            if (loc.line >= startLine) {
                return fid;
            }
        }
        return -1;
    }
};

VarLocResult locateVariable(const FunctionLocator &locator, const Location &loc,
                            bool succFirst, int previousFid, int nextFid,
                            bool isNpeSource) {
    int fid = locator.getFid(loc);
    if (fid == -1) {
        return VarLocResult();
    }

    auto ASTFromFile = getASTOfFile(loc.file);
    auto AST = ASTFromFile->getAST();
    if (!AST)
        return VarLocResult();
    ASTContext &Context = AST->getASTContext();
    auto *TUD = Context.getTranslationUnitDecl();

    fif functionsInFile;
    if (!TUD->isUnavailable())
        FunctionAccumulator(functionsInFile).TraverseDecl(TUD);

    /*
    由于精确匹配可能导致对于 p = foo() 这样的语句，
    匹配不到 foo()，所以全部模糊匹配。

    auto exactResult = locateVariable(functionsInFile, loc.file, loc.line,
                                      loc.column, isStmt, true);
    if (exactResult.isValid())
        return exactResult;
    // 精确匹配失败
    logger.warn("Unable to find exact match! Trying inexact matching...");
    logger.warn("  {}:{}:{}", loc.file, loc.line, loc.column);
    */
    auto result =
        locateVariable(functionsInFile, loc.file, loc.line, loc.column, true,
                       false, succFirst, previousFid, nextFid, isNpeSource);
    return result;
}

void dumpICFGNode(int u, ordered_json &jPath, int beginSid = 0,
                  bool dumpFirstVar = false) {
    auto [fid, bid] = Global.icfg.functionBlockOfNodeId[u];
    requireTrue(fid != -1);

    const NamedLocation &loc = Global.functionLocations[fid];

    logger.trace(">> Node {} is in {} B{}", u, loc.name, bid);

    auto ASTFromFile = getASTOfFile(loc.file);
    auto AST = ASTFromFile->getAST();
    requireTrue(AST != nullptr);

    fif functionsInFile;
    ASTContext &Context = AST->getASTContext();
    auto *TUD = Context.getTranslationUnitDecl();
    requireTrue(!TUD->isUnavailable());
    FunctionAccumulator(functionsInFile).TraverseDecl(TUD);

    for (const auto &fi : functionsInFile.at(loc.file)) {
        if (fi->signature != Global.functionLocations[fid].name)
            continue;
        for (auto BI = fi->cfg->begin(); BI != fi->cfg->end(); ++BI) {
            const CFGBlock &B = **BI;
            if (B.getBlockID() != bid)
                continue;

            bool isEntry = (&B == &fi->cfg->getEntry());
            bool isExit = (&B == &fi->cfg->getExit());
            if (isEntry || isExit) {
                ordered_json j;
                j["type"] = isEntry ? "entry" : "exit";
                // TODO: content只要declaration就行，不然太大了
                // 尽量获取函数定义对应的源码
                const FunctionDecl *D =
                    fi->D->getDefinition() ? fi->D->getDefinition() : fi->D;
                saveLocationInfo(Context, D->getSourceRange(), j);
                jPath.push_back(j);

                goto dumpICFGNodeExit;
            }

            // B.dump(fi->cfg, Context.getLangOpts(), true);

            std::vector<const Stmt *> allStmts;
            std::set<const Stmt *> isChild;

            // iterate over all elements to find stmts & record children
            for (int sid = 0; sid < fi->G[bid].size(); sid++) {
                const Stmt *S = fi->G[bid][sid];
                if (sid >= beginSid)
                    allStmts.push_back(S);

                // iterate over childern
                for (const Stmt *child : S->children()) {
                    if (child != nullptr)
                        isChild.insert(child);
                }
            }

            // print all non-child stmts
            NpeBugSourceVisitor visitor(&Context, fid);
            bool first = true;
            for (const Stmt *S : allStmts) {
                if (isChild.find(S) != isChild.end())
                    continue;
                // S is not child of any stmt in this CFGBlock

                // auto bLoc =
                //     Location::fromSourceLocation(Context, S->getBeginLoc());
                // auto eLoc =
                //     Location::fromSourceLocation(Context, S->getEndLoc());
                // llvm::errs()
                //     << "  Stmt " << bLoc->line << ":" << bLoc->column << " "
                //     << eLoc->line << ":" << eLoc->column << "\n";
                // S->dumpColor();

                ordered_json j;
                j["type"] = "stmt";
                saveLocationInfo(Context, S->getSourceRange(), j);
                if (dumpFirstVar && first)
                    visitor.dump(S, j);
                j["stmtKind"] = std::string(S->getStmtClassName());
                jPath.push_back(j);
                first = false;
            }

            goto dumpICFGNodeExit;
        }
    }

dumpICFGNodeExit:
    return;
}

/**
 * 删除路径中连续重复的 stmt
 */
void deduplicateAndFixLocations(ordered_json &locations, int fromLine,
                                int toLine) {
    std::set<std::string> interestedFields = {
        "type", "file", "beginLine", "beginColumn", "endLine", "endColumn"};

    std::deque<ordered_json> result;
    ordered_json lastEntry;
    for (const auto &j : locations) {
        if (j["type"] != "stmt") {
            result.push_back(j);
            lastEntry.clear();
            continue;
        }

        if (allFieldsMatch(j, lastEntry, interestedFields)) {
            logger.warn("Skipping duplicated stmt in path (most likely due to "
                        "macro expansion)");
            continue;
        }

        result.push_back(j);
        lastEntry = j;
    }

    // 输出会包含 BB 中的所有语句。
    // 如果 source 不是 BB 中第一条，它前面的语句也会被输出。
    // 这里删除路径中，可能存在的 source 之前的、sink 之后的语句。
    while (!result.empty() && result.front()["endLine"] < fromLine)
        result.pop_front();
    while (!result.empty() && result.back()["beginLine"] > toLine)
        result.pop_back();

    locations = result;
}

void saveAsJson(int fromLine, int toLine,
                const std::set<std::vector<int>> &results,
                const std::string &type, int sourceIndex,
                ordered_json &jResults, int fromSid) {
    std::vector<std::vector<int>> sortedResults(results.begin(), results.end());
    // sort based on length
    std::sort(sortedResults.begin(), sortedResults.end(),
              [](const std::vector<int> &a, const std::vector<int> &b) {
                  return a.size() < b.size();
              });
    int cnt = 0;
    for (const auto &path : sortedResults) {
        if (cnt++ > 10)
            break;
        ordered_json jPath, locations;
        jPath["type"] = type;
        jPath["sourceIndex"] = sourceIndex; // input.json 中 results 对应的下标
        if (!Global.noNodes)
            jPath["nodes"] = path;
        dumpICFGNode(path[0], locations, fromSid, type == "npe-bug");
        for (size_t i = 1; i < path.size(); i++) {
            dumpICFGNode(path[i], locations);
        }
        deduplicateAndFixLocations(locations, fromLine, toLine);
        jPath["locations"] = locations;
        jResults.push_back(jPath);
    }
}

/*
 * 返回生成路径的个数
 */
int findPathBetween(const VarLocResult &from, int fromLine, VarLocResult to,
                    int toLine, const std::vector<VarLocResult> &_pointsToPass,
                    const std::vector<VarLocResult> &_pointsToAvoid,
                    const std::string &type, int sourceIndex,
                    ordered_json &jResults) {
    requireTrue(from.isValid(), "FROM location is invalid");
    requireTrue(to.isValid(), "TO location is invalid");

    ICFG &icfg = Global.icfg;
    int u = icfg.getNodeId(from.fid, from.bid);
    int v = icfg.getNodeId(to.fid, to.bid);

    std::vector<int> pointsToPass;
    for (const auto &loc : _pointsToPass) {
        requireTrue(loc.isValid());
        pointsToPass.push_back(icfg.getNodeId(loc.fid, loc.bid));
    }
    std::set<int> pointsToAvoid;
    for (const auto &loc : _pointsToAvoid) {
        requireTrue(loc.isValid());
        pointsToAvoid.insert(icfg.getNodeId(loc.fid, loc.bid));
    }

    auto pFinder = DfsPathFinder(icfg);
    pFinder.search(u, v, pointsToPass, pointsToAvoid, Global.callDepth);

    saveAsJson(fromLine, toLine, pFinder.results, type, sourceIndex, jResults,
               from.sid);
    return pFinder.results.size();
}

// 根据有缺陷的 source 位置，删除可疑的 source
void removeBadSource(const std::string &sourceFile, int sourceLine,
                     SrcSet &suspectedSources) {
    for (auto it = suspectedSources.begin(); it != suspectedSources.end();) {
        const auto &loc = **it; // iterator -> shared_ptr -> json
        const std::string &file = loc["file"];
        int beginLine = loc["beginLine"];
        int endLine = loc["endLine"];

        if (beginLine <= sourceLine && sourceLine <= endLine &&
            file == sourceFile) {
            logger.info("Removing suspected good source: {}:{}:{}", file,
                        beginLine, loc["beginColumn"]);
            it = suspectedSources.erase(it);
        } else {
            it++;
        }
    };
};
void removeBadSourceFromResults(ordered_json &results,
                                SrcSet &suspectedSources) {
    for (const auto &path : results) {
        if (!path.contains("locations"))
            continue;
        for (const auto &loc : path["locations"]) {
            if (loc.contains("type") && loc["type"] == "stmt" &&
                loc.contains("file") && loc.contains("beginLine")) {
                removeBadSource(loc["file"], loc["beginLine"],
                                suspectedSources);
            }
        }
    }
};

void removeNpeBadSource(const std::string &sourceFile, int sourceLine) {
    removeBadSource(sourceFile, sourceLine, Global.npeSuspectedSources);
};
void removeResourceLeakBadSource(const std::string &sourceFile,
                                 int sourceLine) {
    removeBadSource(sourceFile, sourceLine,
                    Global.resourceLeakSuspectedSources);
};
void removeDoubleFreeBadSource(const std::string &sourceFile, int sourceLine) {
    removeBadSource(sourceFile, sourceLine, Global.doubleFreeSuspectedSources);
};

void handleInputEntry(const VarLocResult &from, int fromLine, VarLocResult to,
                      int toLine, const std::vector<VarLocResult> &path,
                      const std::string &type, int sourceIndex,
                      ordered_json &jFinalResults) {

    // 获取 loc 所在函数的出口
    auto getExit = [](const VarLocResult &loc) {
        requireTrue(loc.isValid());
        int fid = loc.fid;
        return VarLocResult(fid, Global.icfg.entryExitOfFunction[fid].second);
    };

    auto requireFromValid = [&from]() {
        requireTrue(from.isValid(), "FROM location invalid");
    };
    auto requireToValid = [&to]() {
        requireTrue(to.isValid(), "TO location invalid");
    };

    // 用于暂时存放路径生成结果
    ordered_json results;

    if (type == "npe") {
        logger.info("Handle known type: {}", type);
        requireFromValid();
        requireToValid();

        logger.info("Generating NPE bug version ...");
        int size = findPathBetween(from, fromLine, to, toLine, path, {},
                                   "npe-bug", sourceIndex, results);
        if (size == 0) {
            logger.warn("Unable to find any path for NPE bug version!");
        } else {
            // 路径经过的所有 stmt 都认为 NPE bad source
            removeBadSourceFromResults(results, Global.npeSuspectedSources);
        }

        if (Global.npeFix) {
            // 无缺陷版本：source -> sink 所在函数的出口
            // 尽量符合原始缺陷路径。如果找不到，就一步步减小路径
            logger.info("Generating NPE fix version ...");
            auto sinkExit = getExit(to);
            std::vector<VarLocResult> p = path;
            bool found = false;
            while (true) {
                int result =
                    findPathBetween(from, fromLine, sinkExit, INT_MAX, p, {to},
                                    "npe-fix", sourceIndex, results);
                if (result) {
                    found = true;
                    break;
                }
                if (p.empty()) // no more path to find
                    break;
                p.pop_back();
            }
            if (!found)
                logger.warn("Unable to find any path for NPE fix version!");
        }

        auto &fromFile = Global.functionLocations[from.fid].file;
        removeNpeBadSource(fromFile, fromLine);
    } else if (type == "npe-bad-source") {
        logger.info("Removing bad NPE source ...");
        requireFromValid();

        auto &fromFile = Global.functionLocations[from.fid].file;
        removeNpeBadSource(fromFile, fromLine);
    } else if (type == "resourceLeak") {
        /**
         * resourceLeak 也有 source 和 sink
         * - source 是 malloc 的位置
         * - sink 对应 last access 的位置（Infer）
         *
         * 在处理时，把 sink 拆分为两个 target:
         * - sink 对应语句
         * - sink 所在函数的 exit
         * 也就是说，路径必须经过 sink 然后到 sink 所在函数的出口
         *
         * 目前的问题：
         * - 如果是在 if 之类的 local block 中 leak，路径还是会到达函数出口。
         *   但这种情况 Infer 也无法区分（因为它给出 last access 的位置，
         *   而非 leak 的位置） ，所以暂时不处理。
         */
        logger.info("Handle known type: {}", type);
        requireFromValid();
        requireToValid();

        auto new_path = std::vector<VarLocResult>(path);
        new_path.push_back(to);

        int size =
            findPathBetween(from, fromLine, getExit(to), INT_MAX, new_path, {},
                            "resourceLeak-bug", sourceIndex, results);
        if (size == 0) {
            logger.warn("Unable to find any path for resource leak!");
        } else {
            // 路径经过的所有 stmt 都认为 RL bad source
            removeBadSourceFromResults(results,
                                       Global.resourceLeakSuspectedSources);
        }
    } else if (type == "resourceLeak-bad-source") {
        logger.info("Removing bad Resource Leak source ...");
        requireFromValid();

        auto &fromFile = Global.functionLocations[from.fid].file;
        removeResourceLeakBadSource(fromFile, fromLine);
    } else if (type == "doubleFree") {
        logger.info("Handle known type: {}", type);
        requireFromValid();
        requireToValid();

        int size = findPathBetween(from, fromLine, to, toLine, path, {},
                                   "doubleFree-bug", sourceIndex, results);
        if (size == 0) {
            logger.warn("Unable to find any path for double free!");
        } else {
            // 路径经过的所有 stmt 都认为 DF bad source
            removeBadSourceFromResults(results,
                                       Global.doubleFreeSuspectedSources);
        }
    } else if (type == "doubleFree-bad-source") {
        logger.info("Removing bad Double Free source ...");
        requireFromValid();

        auto &fromFile = Global.functionLocations[from.fid].file;
        removeDoubleFreeBadSource(fromFile, fromLine);
    } else {
        logger.info("Handle unknown type: {}", type);
        requireFromValid();
        if (!to.isValid()) {
            logger.warn("Missing sink! Using exit of source instead");
            to = getExit(from);
            toLine = INT_MAX;
        }
        findPathBetween(from, fromLine, to, toLine, path, {}, type, sourceIndex,
                        results);
    }

    // 将生成的路径结果加入到最终结果中
    jFinalResults.insert(jFinalResults.end(), results.begin(), results.end());
}

void generatePathFromOneEntry(int sourceIndex, const ordered_json &sourceEntry,
                              FunctionLocator &locator,
                              ordered_json &jResults) {
    std::string bugType = sourceEntry["type"].template get<std::string>();

    const ordered_json &locations = sourceEntry["locations"];
    VarLocResult from, to;
    int fromLine, toLine;
    std::vector<VarLocResult> path;
    int previousFid = -1;
    for (auto it = locations.cbegin(); it != locations.cend(); it++) {
        const ordered_json &loc = *it;
        std::string type = loc["type"].template get<std::string>();
        if (type != "source" && type != "sink" && type != "stmt") {
            logger.warn("Skipping path type: {}", type);
            continue;
        }

        // 目前把 source 和 sink 都当作 stmt 来处理
        // 全部模糊匹配

        // 对于 source 和 sink，优先访问 succ
        bool succFirst = (type != "stmt");
        Location jsonLoc(loc);

        // 跳过项目以外的库函数路径
        if (!Global.isUnderProject(jsonLoc.file)) {
            logger.warn("Skipping lib function in {}", jsonLoc.file);
            continue;
        }

        int nextFid = -1;
        // 获取下一条 可定位 语句对应的 fid
        for (auto nextIt = std::next(it); nextIt != locations.cend();
             nextIt++) {
            // 一直往下找，知道找到 fid 正确的语句
            Location nextJsonLoc(*nextIt);
            nextFid = locator.getFid(nextJsonLoc);
            if (nextFid != -1)
                break;
        }

        VarLocResult varLoc =
            locateVariable(locator, jsonLoc, succFirst, previousFid, nextFid,
                           bugType == "npe" && type == "source");
        if (!varLoc.isValid()) {
            logger.error("Error: cannot locate {} at {}", type, loc);
            // 跳过无法定位的中间路径
            if (type == "stmt")
                continue;
            else
                throw std::runtime_error("Can't locate input entry");
        }

        if (type == "source") {
            from = varLoc;
            fromLine = jsonLoc.line;
        } else if (type == "sink") {
            to = varLoc;
            toLine = jsonLoc.line;
            // sink is the last stmt
            break;
        } else {
            // source is the first stmt
            if (!from.isValid())
                continue;
            path.emplace_back(varLoc);
        }
        previousFid = varLoc.fid;
    }

    handleInputEntry(from, fromLine, to, toLine, path, bugType, sourceIndex,
                     jResults);
}

void dumpSourceToOutput(const SrcSet &sources, //
                        const std::string &type, ordered_json &results) {
    for (const auto &p : sources) {
        const auto &loc = *p;
        ordered_json j;
        j["type"] = type;
        j["locations"].push_back(loc);
        results.push_back(j);
    }
}

/**
 * 从输入的 input.json 中生成路径。
 *
 * @param input 输入的 JSON
 * @param beginIndex 开始下标，inclusive
 * @param endIndex 结束下标，exclusive，如果是-1表示到最后
 */
ordered_json generateFromInput(const ordered_json &input, int beginIndex,
                               int endIndex) {
    logger.info("--- Path-finding ---");

    FunctionLocator locator;

    int total = input["results"].size();

    if (endIndex == -1)
        endIndex = total;
    requireTrue(beginIndex >= 0 && beginIndex <= endIndex && endIndex <= total,
                "Invalid begin/end index");

    logger.info("There are {} results, of which {} will be searched: [{}, {})",
                total, endIndex - beginIndex, beginIndex, endIndex);

    ordered_json output(input);
    // 先把 results 删除，然后加上 source，最后加上空的 results
    output.erase("results");
    output["source"] = Global.inputJsonPath;
    output["results"] = ordered_json::array();

    int index = 0;
    for (int index = beginIndex; index < endIndex; index++) {
        const ordered_json &result = input["results"][index];
        std::string type = result["type"].template get<std::string>();
        logger.info("[{}/{}) type: {}", index, endIndex, type);
        try {
            generatePathFromOneEntry(index, result, locator, output["results"]);
        } catch (const std::exception &e) {
            logger.error("Exception encountered: {}", e.what());
        }
    }

    if (!Global.noGoodSource) {
        dumpSourceToOutput(Global.npeSuspectedSources, "npe-good-source",
                           output["results"]);
        dumpSourceToOutput(Global.resourceLeakSuspectedSources,
                           "resourceLeak-good-source", output["results"]);
        dumpSourceToOutput(Global.doubleFreeSuspectedSources,
                           "doubleFree-good-source", output["results"]);
    }

    return output;
}

ordered_json dumpFuncList() {
    logger.info("Generating function list ...");
    ordered_json output;
    for (size_t i = 0; i < Global.functionLocations.size(); i++) {
        const auto &loc = Global.functionLocations[i];
        const auto &endLoc = Global.functionEndLocations[i];
        logger.trace("  {}: {}", i, loc.name);

        ordered_json j;
        j["signature"] = loc.name;
        j["file"] = loc.file;
        j["beginLine"] = loc.line;
        j["beginColumn"] = loc.column;
        j["endLine"] = endLoc.line;
        j["endColumn"] = endLoc.column;

        output.push_back(j);
    }
    return output;
}

/**
 * 将 input.json 中 mayMalloc 和 mayFree 等手工指定的函数，添加到集合中。
 * defaults 是集合中默认会添加的函数。
 */
void addFromInput(const ordered_json &input, std::string key,
                  std::set<std::string> &output,
                  std::initializer_list<std::string> defaults) {
    // 加入一些默认的函数
    output.insert(defaults.begin(), defaults.end());
    // 从输入的 key 中加入函数
    if (input.contains(key)) {
        for (const auto &f : input[key]) {
            output.insert(f.get<std::string>());
        }
    }
}

void writeJsonToFile(const std::string &title, const ordered_json &j,
                     const fs::path &path) {
    std::ofstream o(path);
    o << j.dump(4, ' ', false, json::error_handler_t::replace) << std::endl;
    // 判断是否成功写入
    requireTrue(o.good(), "Failed to write to " + path.string());
    logger.info("{} wrote to {}", title, path);
    o.close();
}

int getArgValue(args::ValueFlag<int> &arg, const int defaultValue,
                const std::string &name) {
    int v = defaultValue;
    if (arg) {
        v = args::get(arg);
        requireTrue(v >= 1, name + " must be greater than 0");
    }
    logger.info("{}: {}", name, v);
    return v;
}

bool getArgValue(args::Flag &arg, const std::string &name) {
    bool v = false;
    if (arg) {
        v = true;
    }
    logger.info("{}: {}", name, v);
    return v;
}

int main(int argc, const char **argv) {
    spdlog::set_level(spdlog::level::debug);

    args::ArgumentParser argParser(
        "Path generation tool\n"
        "Example:\n"
        "  ./tool npe/input.json\n"
        "  ./tool -d10 -t60 npe/input.json\n"
        "  ./tool --no-good-source npe/input.json\n");

    args::HelpFlag help(argParser, "help", "Display help menu", {'h', "help"});

    args::Flag argFuncList(argParser, "func-list",
                           "Generate function list only", {"func-list"});

    // 指定搜索路径的下标范围: [begin, end)
    args::ValueFlag<int> argBeginIndex(
        argParser, "N", "Begin index (inclusive, default 0)", {'b', "begin"});
    args::ValueFlag<int> argEndIndex(
        argParser, "N", "End index (exclusive, default -1)", {'e', "end"});

    args::ValueFlag<int> argPoolSize(
        argParser, "N",
        "AST Pool size (max number of ASTs in memory), default 10", {'p'});
    args::ValueFlag<int> argCallDepth(
        argParser, "N", "Max call depth in path-finding, default 6", {'d'});
    args::ValueFlag<int> argDfsTick(
        argParser, "N",
        "DFS tick (timeout checking frequency), default 1'000'000", {"tick"});
    args::ValueFlag<int> argDfsTimeout(
        argParser, "N", "DFS timeout in seconds, default 30", {'t'});
    args::Flag argKeepAST(argParser, "keep-ast", "Keep AST dumps on disk",
                          {"keep-ast"});
    args::Flag argNoGoodSource(argParser, "no-good-source",
                               "Do not generate xxx-good-source",
                               {"no-good-source"});
    args::Flag argNoNodes(argParser, "no-nodes", "Do not dump ICFG nodes",
                          {"no-nodes"});
    args::Flag argNpeFix(argParser, "npe-fix",
                         "Generate npe-fix (default false)", {"npe-fix"});
    args::Flag argDebug(argParser, "debug", "Debug mode", {"debug"});

    args::Flag argVersion(argParser, "version", "Display version", {"version"});

    args::Positional<std::string> argIR(argParser, "IR", "Path to input.json",
                                        {args::Options::Required});

    try {
        argParser.ParseCLI(argc, argv);
    } catch (const args::Help &) {
        std::cout << argParser;
        return 0;
    } catch (const args::ParseError &e) {
        std::cerr << e.what() << std::endl;
        std::cerr << argParser;
        return 1;
    } catch (const args::ValidationError &e) {
        // 通常是因为没有给 IR，但如果给了 version，就跳过
        if (!argVersion) {
            std::cerr << e.what() << std::endl;
            std::cerr << argParser;
            return 1;
        }
    }

    if (argVersion) {
        std::cout << "Tool " << clang::getClangFullRepositoryVersion()
                  << std::endl;
        return 0;
    }

    logger.info("AST & ICFG generation method: sequential");

    Global.ASTPoolSize = getArgValue(argPoolSize, 10, "AST pool size");
    Global.callDepth = getArgValue(argCallDepth, 6, "Max call depth");
    Global.dfsTick = getArgValue(argDfsTick, 1'000'000, "DFS tick");
    Global.dfsTimeout = getArgValue(argDfsTimeout, 30, "DFS timeout");
    Global.keepAST = getArgValue(argKeepAST, "Keep AST");
    Global.noGoodSource = getArgValue(argNoGoodSource, "No good-source");
    Global.noNodes = getArgValue(argNoNodes, "No nodes");
    Global.npeFix = getArgValue(argNpeFix, "NPE fix");
    Global.debug = getArgValue(argDebug, "Debug");

    setClangPath(argv[0]);

    llvm::InitLLVM X(argc, argv);

    fs::path jsonPath = fs::absolute(args::get(argIR));
    Global.inputJsonPath = jsonPath;
    std::ifstream ifs(jsonPath);
    if (!ifs.is_open()) {
        logger.error("Cannot open file {}", jsonPath);
        return 1;
    }
    logger.info("Reading from input json: {}", jsonPath);
    ordered_json input = ordered_json::parse(ifs);

    Global.projectDirectory =
        fs::canonical(input["root"].template get<std::string>()).string();

    fs::path compile_commands =
        fs::canonical(input["compile_commands"].template get<std::string>());
    logger.info("Compilation database: {}", compile_commands);
    fixCompilationDatabase(compile_commands);
    Global.cb = getCompilationDatabaseWithASTEmit(compile_commands);
    {
        auto allFiles = Global.cb->getAllFiles();
        Global.allFiles =
            std::set<std::string>(allFiles.begin(), allFiles.end());
    }

    // resourceLeak-good-source
    addFromInput(input, "mayMalloc", Global.mayMallocFunctions,
                 {"malloc" /* , "calloc", "realloc" */});
    // doubleFree-good-source
    addFromInput(input, "mayFree", Global.mayFreeFunctions,
                 {"free" /* , "realloc" */});

    generateICFG(*Global.cb);

    // npe-good-source
    {
        std::set<std::string> mayNullFunctions;
        // 把 input.json 中 mayNull 的函数名加入到 mayNullFunctions
        addFromInput(input, "mayNull", mayNullFunctions, {});
        // 将 input.json 中手工指定的函数加入返回图中，并计算 mayNull
        Global.returnGraph.propagate(mayNullFunctions);

        auto sources =
            std::set<SrcPtr, SrcPtrCompare>(Global.npeSuspectedSources.begin(),
                                            Global.npeSuspectedSources.end());

        // 更新 sources：
        // 对所有 p = foo()，把函数中没有 return NULL 语句的都删掉
        for (auto p : Global.npeSuspectedSourcesFunMap) {
            const std::string &signature = p.first;

            // input.json 中手工指定的函数不会被删除
            if (!mayNullFunctions.empty() && // 仅在需要时才获取函数名
                mayNullFunctions.find(getNameFromFullSignature(signature)) !=
                    mayNullFunctions.end())
                continue;

            if (Global.returnGraph.mayNull(signature) == false)
                for (auto weakPtr : p.second) {
                    // 如果 weakPtr 对应的 shared_ptr 还存在，就删除
                    if (auto p = weakPtr.lock())
                        sources.erase(p);
                }
        }

        Global.npeSuspectedSources =
            std::vector<SrcPtr>(sources.begin(), sources.end());
    }

    {
        int m = 0;
        for (const auto &edges : Global.icfg.G) {
            m += edges.size();
        }
        logger.info("ICFG: {} nodes, {} edges", Global.icfg.n, m);
    }

    {
        logger.info("Calculating reachability ...");

        Global.icfg.calculateReachability();

        int unreachable = 0;
        for (int r : Global.icfg.nodeVisited) {
            unreachable += !r;
        }
        logger.info("Unreachable nodes: {}", unreachable);
    }

    fs::path outputDir = jsonPath.parent_path();

    if (argFuncList) {
        writeJsonToFile("Function list", dumpFuncList(),
                        outputDir / "func-list.json");
    } else {
        int beginIndex = getArgValue(argBeginIndex, 0, "Begin index");
        int endIndex = getArgValue(argEndIndex, -1, "End index");
        auto result = generateFromInput(input, beginIndex, endIndex);
        writeJsonToFile("Output", result, outputDir / "output.json");
    }

    // 用于调试，查询每个 node 的信息
    while (Global.debug) {
        int node;
        fmt::print("> ");
        std::cin >> node;
        if (!std::cin)
            break;

        // print all successors & edge type
        const auto &G = Global.icfg.G[node];

        auto [fid, bid] = Global.icfg.functionBlockOfNodeId[node];
        fmt::print("Node {} is in {} B{}\n", node,
                   Global.functionLocations[fid].name, bid);

        ordered_json dump;
        dumpICFGNode(node, dump);
        // 优先处理 entry & exit
        if (dump.size() == 1) {
            auto &j = dump[0];
            std::string type = j["type"].get<std::string>();
            if (type != "stmt") {
                fmt::print("{} of function:\n", type);
                fmt::print("  {}\n", j["content"].get<std::string>());
                continue;
            }
        }

        // stmt
        fmt::print("{} stmts:\n", dump.size());
        for (const auto &j : dump) {
            std::string kind = j["stmtKind"].get<std::string>();
            fmt::print(" {}:\n",
                       fmt::format(fg(fmt::color::green), "{}", kind));
            fmt::print("  {}\n", j["content"].get<std::string>());
        }

        fmt::print("{} successors:\n", G.size());
        for (const auto &e : G) {
            std::string type = e.type == ICFG::Edge::Type::INTRA_PROC ? "intra"
                               : e.type == ICFG::Edge::Type::CALL_EDGE
                                   ? "call"
                                   : "return";
            fmt::print("  {} {}\n", e.target, type);
        }
    }

    return 0;

    while (true) {
        std::string methodName;
        fmt::print("> ");
        std::getline(std::cin, methodName);
        if (!std::cin)
            break;
        if (methodName.find('(') == std::string::npos)
            methodName += "(";

        for (const auto &[caller, callees] : Global.callGraph) {
            if (caller.find(methodName) != 0)
                continue;
            fmt::print("{}\n", caller);
            for (const auto &callee : callees) {
                fmt::print("  {}\n", callee);
            }
        }
    }
}
