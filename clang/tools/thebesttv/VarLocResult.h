#pragma once

#include "FunctionInfo.h"
#include "utils.h"

struct VarLocResult {
    int fid, bid, sid;

    VarLocResult() : fid(-1), bid(-1), sid(-1) {}
    VarLocResult(int fid, int bid, int sid = -1)
        : fid(fid), bid(bid), sid(sid) {}
    VarLocResult(const std::unique_ptr<FunctionInfo> &fi, int bid, int sid = -1)
        : fid(Global.getIdOfFunction(fi->signature, fi->file)), bid(bid),
          sid(sid) {}
    VarLocResult(const std::unique_ptr<FunctionInfo> &fi, const CFGBlock *block,
                 int sid = -1)
        : VarLocResult(fi, block->getBlockID(), sid) {}

    bool operator==(const VarLocResult &other) const {
        return fid == other.fid && bid == other.bid && sid == other.sid;
    }
    bool operator!=(const VarLocResult &other) const {
        return !(*this == other);
    }

    bool isValid() const { return fid != -1; }
};
