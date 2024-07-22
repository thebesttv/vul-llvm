#include "base.h"
#include "../DumpPath.h"

const FunctionDecl *BaseMatcher::getDirectCallee(const Expr *E) {
    // 如果是 CastExpr，递归查找
    if (const CastExpr *expr = dyn_cast<CastExpr>(E)) {
        return getDirectCallee(expr->getSubExpr());
    }

    if (const CallExpr *expr = dyn_cast<CallExpr>(E)) {
        return expr->getDirectCallee();
    }
    return nullptr;
}

bool BaseMatcher::isPointerType(const Expr *E) {
    if (!E)
        return false;
    auto type = E->getType().getTypePtrOrNull();
    return type && type->isAnyPointerType();
}

std::optional<ordered_json>
BaseMatcher::dumpNpeSource(const SourceRange &range,
                           const std::optional<SourceRange> &varRange) {
    ordered_json loc;
    // something wrong with location
    if (!saveLocationInfo(*Context, range, loc))
        return std::nullopt;
    // source outside current project
    const std::string &file = loc["file"];
    if (!Global.isUnderProject(file))
        return std::nullopt;

    // 导出变量位置
    if (varRange.has_value()) {
        ordered_json varLoc;
        if (!saveLocationInfo(*Context, varRange.value(), varLoc))
            return std::nullopt;
        varLoc.erase("file"); // 肯定是同一个文件
        loc["variable"] = varLoc;
    }
    return loc;
}
