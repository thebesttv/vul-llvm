#include "base.h"
#include "../DumpPath.h"

const Expr *BaseMatcher::uncast(const Expr *E) {
    if (!E)
        return nullptr;
    while (true) {
        if (const auto *CE = dyn_cast<CastExpr>(E)) {
            E = CE->getSubExpr();
        } else if (const auto *PE = dyn_cast<ParenExpr>(E)) {
            E = PE->getSubExpr();
        } else {
            break;
        }
    }
    return E;
}

const FunctionDecl *BaseMatcher::getDirectCallee(const Expr *E) {
    if (!E)
        return nullptr;
    E = uncast(E);
    if (const CallExpr *expr = dyn_cast<CallExpr>(E)) {
        return ::getDirectCallee(expr);
    }
    return nullptr;
}

bool BaseMatcher::isPointerType(const Expr *E) {
    if (!E)
        return false;
    auto type = E->getType().getTypePtrOrNull();
    // 对于 issue 242 main.cpp 第 9 行 nullptr 的对应类型
    // isAnyPointerType() 返回 false，isNullPtrType() 返回 true
    return type && (type->isAnyPointerType() || type->isNullPtrType());
}

std::optional<ordered_json>
BaseMatcher::dumpSource(const SourceRange &range,
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
        if (!saveLocationInfo(*Context, varRange.value(), varLoc, false))
            return std::nullopt;
        // 由于宏，变量可能不在同一个文件中
        // varLoc.erase("file");
        loc["variable"] = varLoc;
    }
    return loc;
}
