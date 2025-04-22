
#include "DumpPath.h"
#include "base.h"

/**
 * 加入以下情况
 * - 变量定义 & 赋值
 * - 指针
 * - 数组
 * - vector, array, string
 */
class BufferOverflowGoodSourceVisitor
    : public RecursiveASTVisitor<BufferOverflowGoodSourceVisitor>,
      public BaseMatcher {
  private:
    std::optional<SrcWeakPtr>
    saveSuspectedSource(const SourceRange &range,
                        const std::optional<SourceRange> &varRange) {
        static int index = 0;
        return BaseMatcher::saveSuspectedSource(
            range, varRange, Global.bufferOverflowSuspectedSources, index);
    }

    bool isArrayType(const QualType &type) {
        return type->isArrayType() || type->isConstantArrayType() ||
               type->isIncompleteArrayType() || type->isVariableArrayType() ||
               type->isDependentSizedArrayType();
    }
    bool isPointerType(const QualType &type) {
        return type->isAnyPointerType();
    }

  public:
    explicit BufferOverflowGoodSourceVisitor(ASTContext *Context, int fid)
        : BaseMatcher(Context, fid) {}

    bool VisitVarDecl(VarDecl *D) {
        // must be declared within a function
        if (!D->getParentFunctionOrMethod())
            return true;

        const auto &type = D->getType();
        if (isArrayType(type) || isPointerType(type)) {
            saveSuspectedSource(D->getSourceRange(), D->getLocation());
        } else if (type->isClassType() || type->isStructuralType()) {
            const auto &ctype = type.getCanonicalType();
            if (const auto &decl = ctype->getAsCXXRecordDecl()) {
                const auto &name = decl->getNameAsString();
                if (name == "vector" || name == "array" ||
                    name == "basic_string") {
                    saveSuspectedSource(D->getSourceRange(), D->getLocation());
                }
            }
        }
        return true;
    }
};

class BufferOverflowBugSourceVisitor
    : public BaseMatcher,
      public RecursiveASTVisitor<BufferOverflowBugSourceVisitor> {

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

        for (const auto &varLoc : original) {
            Stmt *stmt = const_cast<Stmt *>(G[varLoc.bid][varLoc.sid]);

            isMatch = false;

            this->TraverseStmt(stmt);

            if (!isMatch)
                continue;

            auto addToMatches = [&](const SourceRange range) {
                ordered_json j;
                if (saveLocationInfo(*Context, range, j)) {
                    auto x = Item(varLoc, j, line, column);
                    if (x.endLine - x.beginLine > 1)
                        return;

                    matches.push_back(x);
                }
            };

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

            auto range = stmt->getSourceRange();
            addToMatches(range);
            if (auto memberCallExpr = dyn_cast<CXXMemberCallExpr>(stmt)) {
                // p->foo()，其中 beginLoc 是 p，endLoc 是 foo，只需要 endLoc
                // handleSingleSourceLocation(memberCallExpr->getBeginLoc());
                handleSingleSourceLocation(memberCallExpr->getExprLoc());
            } else if (auto callExpr = dyn_cast<CallExpr>(stmt)) {
                handleSingleSourceLocation(callExpr->getExprLoc());
            }
        }

        if (matches.empty())
            return {};

        // 返回最匹配的结果
        std::sort(matches.begin(), matches.end());
        return {matches.front().varLoc};
    }

  public:
    explicit BufferOverflowBugSourceVisitor(ASTContext *Context, int fid)
        : BaseMatcher(Context, fid) {}

    bool VisitVarDecl(VarDecl *D) {
        // must be declared within a function
        if (!D->getParentFunctionOrMethod())
            return true;

        setMatchAndMaybeDumpJson(D->getSourceRange(), D->getLocation());
        return false;
    }

    bool VisitBinaryOperator(BinaryOperator *S) {
        if (S->getOpcode() == BO_Assign) {
            std::optional<SourceRange> varRange = std::nullopt;
            if (S->getLHS()) {
                varRange = getProperSourceRange(S->getLHS());
            }
            setMatchAndMaybeDumpJson(S->getSourceRange(), varRange);
            return false;
        }

        return true;
    }
    bool VisitReturnStmt(ReturnStmt *S) {
        Expr *value = S->getRetValue();
        setMatchAndMaybeDumpJson(S->getSourceRange(), std::nullopt);
        return false;
    }

    std::vector<VarLocResult>
    transform(const std::vector<VarLocResult> original,
              const decltype(FunctionInfo::G) &G, int line, int column) {
        if (original.empty())
            return original;

        dumpJson = false;

        logger.info("In BufferOverflowBugSourceVisitor::transform");

        std::vector<VarLocResult> result =
            traverseAndMatch(original, G, line, column);
        logger.info("> stage1: {}", fmt::join(result, ", "));

        return result;
    }

    void dump(const Stmt *stmt, ordered_json &j) {
        dumpJson = true;
        dumpedJson.clear();
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
