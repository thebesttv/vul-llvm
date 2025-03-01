#pragma once

#include "DumpPath.h"
#include "base.h"
#include <fmt/color.h>

class NpeSourceMatcher : public BaseMatcher {
  private:
    /**
     * 首先，将表达式 uncast，因为：
     * - 可能存在 cast
     * - 宏可能引入括号，如
     *   #define IS_NULL(x) ((x) == NULL)
     *
     * 其次，处理以下情况：
     * - 对于 MemberExpr (o.x, o->x)，若由于宏导致 o 和 x 不在同一个文件中，
     *   则只返回 o
     */
    const Expr *getProperVar(const Expr *E);

  protected:
    bool isNullPointerConstant(const Expr *expr);

    SourceRange getProperSourceRange(const Expr *E) {
        return getProperVar(E)->getSourceRange();
    }

  public:
    explicit NpeSourceMatcher(ASTContext *Context, int fid)
        : BaseMatcher(Context, fid) {}
};

/**
 * source 的保存策略：
 * 1. p = NULL
 * 2. p = foo() && foo() = { ...; return NULL; }
 *    其中第二个判断（foo() 中包含 return NULL）在之后才会做。
 *    目前放到 main() 里做，在 generateICFG() 之后
 * 3. p = foo() && 在 input.json 中指定 foo 可能返回 NULL
 *    同样，判断在 main() 里做
 * 4. p == NULL / p != NULL
 * 5. 对 foo() 解引用，包括 *foo()、foo()[i]、foo()->x。
 *    额外包括 foo() 作为函数入参的情况，如 g(foo())。
 *    此时 variable 中为函数名。函数名从 calleeDecl 中获取。
 */
class NpeGoodSourceVisitor : public RecursiveASTVisitor<NpeGoodSourceVisitor>,
                             public NpeSourceMatcher {

    // 加入 NPE 可疑的 source 中
    std::optional<SrcWeakPtr>
    saveNpeSuspectedSources(const SourceRange &range,
                            const std::optional<SourceRange> &varRange);
    // source 与函数 callee 是否可能返回 null 相关。
    // 如果 callee 可能返回 null，就是 NPE 可疑的 source。
    // 否则，会在之后被删除。
    void saveNpeSuspectedSourcesWithCallee(
        const SourceRange &range, const std::optional<SourceRange> &varRange,
        const FunctionDecl *calleeDecl);
    /**
     * 类似上一个，但把 callee 作为 variable。
     *
     * 之前在 5 的三个函数中，使用 getProperSourceRange(E) 获得函数名。
     * 但由于宏的影响（如 issue 239）会出问题：
     *   #define FOO foo
     *   FOO();
     * 这里 E 的 source range 的 begin 是 FOO，而 end 是括号，
     * 导致 content 可能横跨两个文件。
     *
     * 所以改用 getProperSourceRange(E).getBegin()，
     * 不过这样导致只会加入函数名的第一部分，例如：
     * - foo() -> foo
     * - issue 239 中的 GetCharAddr<TestClass> -> GetCharAddr
     *
     * 现在改为从 calleeDecl 中获取。
     * 虽然还是只有第一部分，不过起码能去掉宏的影响。
     */
    void saveNpeSuspectedSourcesWithCallee(const Stmt *S,
                                           const FunctionDecl *calleeDecl);

    void checkFormPEqNullOrFoo(const SourceRange &range, const Expr *rhs,
                               const std::optional<SourceRange> &varRange);

  public:
    explicit NpeGoodSourceVisitor(ASTContext *Context, int fid)
        : NpeSourceMatcher(Context, fid) {}

    bool VisitVarDecl(VarDecl *D);
    bool VisitBinaryOperator(BinaryOperator *S);
    bool VisitReturnStmt(ReturnStmt *S);

    // 5 中形如 *foo() 的情况
    bool VisitUnaryOperator(UnaryOperator *S);
    // 5 中形如 foo()[i] 的情况
    bool VisitArraySubscriptExpr(ArraySubscriptExpr *S);
    // 5 中形如 foo()->x 的情况
    bool VisitMemberExpr(MemberExpr *S);
    // 5 中 foo() 作为函数入参的情况
    bool VisitCallExpr(CallExpr *expr);
};

class NpeBugSourceVisitor : public RecursiveASTVisitor<NpeBugSourceVisitor>,
                            public NpeSourceMatcher {

    int sid;
    std::vector<std::pair<int, ordered_json>> npeSources, nullConstants;

    enum {
        // return null, p = xxx, p == null, p != null
        // 根据 10 月 12 日讨论，加入 foo() 的情况
        RETURN_NULL_OR_NPE_GOOD_SOURCE,
        // nullptr, NULL, 0
        NULL_CONSTANT
    } currentStage;
    bool isMatch = false;
    bool dumpJson = false;

    ordered_json dumpedJson;

    void setMatchAndMaybeDumpJson(const SourceRange &range,
                                  const std::optional<SourceRange> &varRange) {
        isMatch = true;
        if (dumpJson) {
            auto json = dumpSource(range, varRange);
            if (json) {
                dumpedJson = json.value();
            }
        }
    }

    struct Item {
        VarLocResult varLoc;
        int beginLine, beginColumn, endLine, endColumn;
        int size;
        bool withinRange;

        Item(const VarLocResult &varLoc, const ordered_json &j, int line,
             int column)
            : varLoc(varLoc) {
            beginLine = j["beginLine"];
            beginColumn = j["beginColumn"];
            endLine = j["endLine"];
            endColumn = j["endColumn"];

            withinRange =
                ((line == beginLine && column >= beginColumn) ||
                 (line > beginLine)) &&
                ((line == endLine && column <= endColumn) || (line < endLine));
            size = (endColumn - beginColumn) + (endLine - beginLine) * 100;
        }

        bool operator<(const Item &rhs) const {
            // 优先级：withinRange > size
            if (withinRange != rhs.withinRange)
                return withinRange;
            return size < rhs.size;
        }
    };

    // 用于 debug
    void processSourceLocation(SourceLocation loc, std::string msg) {
        FullSourceLoc fullLoc = Context->getFullLoc(loc);
        if (fullLoc.isInvalid()) {
            logger.error("Invalid location: {}", msg);
            return;
        }
        if (fullLoc.isFileID()) {
            logger.info("> {} F: {}:{}", msg, fullLoc.getLineNumber(),
                        fullLoc.getColumnNumber());
            return;
        }
        logger.info("> {} M", msg);
        // 这个对于 issue 247 比较有用
        processSourceLocation(fullLoc.getImmediateMacroCallerLoc(),
                              "  " + msg + " I");
        processSourceLocation(fullLoc.getSpellingLoc(), "  " + msg + " S");
        processSourceLocation(fullLoc.getExpansionLoc(), "  " + msg + " E");
    }

    /**
     * 根据 source 点的行号、列号进行匹配
     *
     * 对所有匹配的语句，根据以下两个 key 进行排序：
     * - `withinRange`: source 是否在 stmt 以内，以内的优先
     * - `size`: stmt 的大小，小的优先
     */
    std::vector<VarLocResult>
    traverseAndMatch(const std::vector<VarLocResult> original,
                     const decltype(FunctionInfo::G) &G, int line, int column) {
        std::vector<Item> matches;

        // 对所有函数调用，如果参数中有 nullptr，就加入到 nullArgsToInclude 中
        std::set<const Stmt *> nullArgsToInclude;
        for (const auto &varLoc : original) {
            if (auto callExpr = dyn_cast<CallExpr>(G[varLoc.bid][varLoc.sid])) {
                for (auto *_arg : callExpr->arguments()) {
                    auto arg = uncast(_arg);
                    if (isNullPointerConstant(arg)) {
                        nullArgsToInclude.insert(arg);
                    }
                }
            }
        }

        for (const auto &varLoc : original) {
            Stmt *stmt = const_cast<Stmt *>(G[varLoc.bid][varLoc.sid]);

            isMatch = false;
            if (nullArgsToInclude.find(stmt) != nullArgsToInclude.end()) {
                // 某个函数调用的 nullptr 参数，作为 NPE bug source
                auto oldCurrentStage = currentStage;
                currentStage = NULL_CONSTANT;
                this->TraverseStmt(stmt);
                currentStage = oldCurrentStage;
            } else {
                this->TraverseStmt(stmt);
            }
            if (!isMatch)
                continue;

            auto addToMatches = [&](const SourceRange range) {
                ordered_json j;
                if (saveLocationInfo(*Context, range, j)) {
                    auto x = Item(varLoc, j, line, column);
                    if (x.endLine - x.beginLine > 1)
                        return;

                    matches.push_back(x);

                    /*
                    logger.info("Added to matches: begin: {}:{}, end: {}:{}, "
                                "size: {}, within range: {}",
                                x.beginLine, x.beginColumn, x.endLine,
                                x.endColumn, x.size, x.withinRange);
                    auto content = j["content"].get<std::string>();
                    logger.info(
                        fmt::format(fg(fmt::color::red), "{}", content));
                    */
                }
            };

            /*
            auto getSpellingRange = [&](const SourceRange range) {
                auto b = range.getBegin();
                auto e = range.getEnd();
                auto &SM = Context->getSourceManager();
                return SourceRange(SM.getSpellingLoc(b), SM.getSpellingLoc(e));
            };

            auto getExpansionRange = [&](const SourceRange range) {
                auto b = range.getBegin();
                auto e = range.getEnd();
                auto &SM = Context->getSourceManager();
                return SourceRange(SM.getExpansionLoc(b),
                                   SM.getExpansionLoc(e));
            };
            */

            auto getImmediateMacroCallerRange = [&](const SourceRange range) {
                auto b = range.getBegin();
                auto e = range.getEnd();
                auto &SM = Context->getSourceManager();
                return SourceRange(SM.getImmediateMacroCallerLoc(b),
                                   SM.getImmediateMacroCallerLoc(e));
            };

            auto handleSingleSourceLocation = [&](const SourceLocation loc) {
                if (!loc.isMacroID())
                    return;
                addToMatches(getImmediateMacroCallerRange(loc));
            };

            // stmt->dumpColor();
            // processSourceLocation(stmt->getBeginLoc(), "Begin");
            // processSourceLocation(stmt->getEndLoc(), "End");

            auto range = stmt->getSourceRange();
            addToMatches(range);
            if (auto memberCallExpr = dyn_cast<CXXMemberCallExpr>(stmt)) {
                // p->foo()，其中 beginLoc 是 p，endLoc 是 foo，只需要 endLoc
                // handleSingleSourceLocation(memberCallExpr->getBeginLoc());
                handleSingleSourceLocation(memberCallExpr->getExprLoc());
            } else if (auto callExpr = dyn_cast<CallExpr>(stmt)) {
                handleSingleSourceLocation(callExpr->getExprLoc());
            }
            // addToMatches(getSpellingRange(range));
            // addToMatches(getExpansionRange(range));
        }

        if (matches.empty())
            return {};

        // 返回最匹配的结果
        std::sort(matches.begin(), matches.end());
        return {matches.front().varLoc};
    }

  public:
    explicit NpeBugSourceVisitor(ASTContext *Context, int fid)
        : NpeSourceMatcher(Context, fid) {}

    bool VisitVarDecl(VarDecl *D) {
        if (currentStage != RETURN_NULL_OR_NPE_GOOD_SOURCE)
            return true;

        // must be declared within a function
        if (!D->getParentFunctionOrMethod())
            return true;

        if (D->hasInit() && isPointerType(D->getInit())) {
            setMatchAndMaybeDumpJson(D->getSourceRange(), D->getLocation());
            return false;
        }

        return true;
    }
    bool VisitBinaryOperator(BinaryOperator *S) {
        if (currentStage != RETURN_NULL_OR_NPE_GOOD_SOURCE)
            return true;

        if (S->getOpcode() == BO_Assign) {
            if (isPointerType(S)) {
                std::optional<SourceRange> varRange = std::nullopt;
                if (S->getLHS()) {
                    varRange = getProperSourceRange(S->getLHS());
                }
                setMatchAndMaybeDumpJson(S->getSourceRange(), varRange);
                return false;
            }
        } else if (S->getOpcode() == BO_EQ || S->getOpcode() == BO_NE) {
            // 两边形如 p == NULL 或 p != NULL
            auto inFormPNeNull = [this](const Expr *l, const Expr *r) {
                return isPointerType(l) && !isNullPointerConstant(l) &&
                       isNullPointerConstant(r);
            };

            // p != NULL 或 NULL != p
            if (inFormPNeNull(S->getLHS(), S->getRHS())) {
                setMatchAndMaybeDumpJson(S->getSourceRange(),
                                         getProperSourceRange(S->getLHS()));
                return false;
            } else if (inFormPNeNull(S->getRHS(), S->getLHS())) {
                setMatchAndMaybeDumpJson(S->getSourceRange(),
                                         getProperSourceRange(S->getRHS()));
                return false;
            }
        }

        return true;
    }
    bool VisitReturnStmt(ReturnStmt *S) {
        if (currentStage != RETURN_NULL_OR_NPE_GOOD_SOURCE)
            return true;
        Expr *value = S->getRetValue();
        if (isNullPointerConstant(value)) {
            setMatchAndMaybeDumpJson(S->getSourceRange(), std::nullopt);
            return false;
        }
        return true;
    }

    bool VisitCallExpr(CallExpr *expr) {
        if (currentStage != RETURN_NULL_OR_NPE_GOOD_SOURCE)
            return true;
        auto calleeDecl = getDirectCallee(expr);
        if (!calleeDecl)
            return true;
        setMatchAndMaybeDumpJson(expr->getSourceRange(),
                                 calleeDecl->getNameInfo().getSourceRange());
        return false;
    }

    bool VisitExpr(Expr *E) {
        if (currentStage != NULL_CONSTANT)
            return true;
        if (isNullPointerConstant(E)) {
            setMatchAndMaybeDumpJson(E->getSourceRange(), std::nullopt);
            return false;
        }
        return true;
    }

    std::vector<VarLocResult>
    transform(const std::vector<VarLocResult> original,
              const decltype(FunctionInfo::G) &G, int line, int column) {
        if (original.empty())
            return original;

        dumpJson = false;

        logger.info("In NpeBugSourceVisitor::transform");

        currentStage = RETURN_NULL_OR_NPE_GOOD_SOURCE;
        std::vector<VarLocResult> result =
            traverseAndMatch(original, G, line, column);
        logger.info("> stage1: {}", fmt::join(result, ", "));

        if (result.empty()) {
            currentStage = NULL_CONSTANT;
            result = traverseAndMatch(original, G, line, column);
            logger.info("> stage2: {}", fmt::join(result, ", "));
        }

        // return result.empty() ? original : result;
        return result;
    }

    void dump(const Stmt *stmt, ordered_json &j) {
        dumpJson = true;
        dumpedJson.clear();
        currentStage = RETURN_NULL_OR_NPE_GOOD_SOURCE;
        isMatch = false;
        if (const DeclStmt *declStmt = dyn_cast<DeclStmt>(stmt)) {
            TraverseDeclStmt(const_cast<DeclStmt *>(declStmt));
        } else if (const BinaryOperator *binOp =
                       dyn_cast<BinaryOperator>(stmt)) {
            VisitBinaryOperator(const_cast<BinaryOperator *>(binOp));
        } else if (const CallExpr *callExpr = dyn_cast<CallExpr>(stmt)) {
            VisitCallExpr(const_cast<CallExpr *>(callExpr));
        }

        if (isMatch && dumpedJson.contains("variable")) {
            j["variable"] = dumpedJson["variable"];
        }
    }
};
