#include "ICFG.h"
#include "utils.h"

int ICFG::getNodeId(int fid, int bid) {
    auto it = nodeIdOfFunctionBlock.find({fid, bid});
    if (it != nodeIdOfFunctionBlock.end())
        return it->second;
    int id = nodeIdOfFunctionBlock.size();
    nodeIdOfFunctionBlock[{fid, bid}] = id;
    functionBlockOfNodeId.push_back({fid, bid});
    return id;
}

void ICFG::addEdge(int u, Edge::Type type, int callSiteId, int v) {
    if (type == Edge::Type::INTRA_PROC)
        requireTrue(callSiteId == 0);
    else
        requireTrue(callSiteId > 0);
    G[u].emplace_back(type, callSiteId, v);
    G_reverse[v].emplace_back(type, callSiteId, u);
}

void ICFG::addNormalEdge(int fid, int uBid, int vBid) {
    int u = getNodeId(fid, uBid);
    int v = getNodeId(fid, vBid);
    // llvm::errs() << "In fun: " << Global.functionLocations[fid]->name << " "
    //              << uBid << " -> " << vBid << "\n";
    addEdge(u, Edge::Type::INTRA_PROC, 0, v);
}

void ICFG::addCallAndReturnEdge(int uEntry, int uExit, int calleeFid) {
    // entry & exit nodes of callee
    auto [entry, exit] = entryExitOfFunction[calleeFid];
    int vEntry = getNodeId(calleeFid, entry);
    int vExit = getNodeId(calleeFid, exit);

    callSiteId++;
    // llvm::errs()
    //     << "Call: "
    //     <<
    //     Global.functionLocations[functionBlockOfNodeId[uEntry].first]->name
    //     << " -> " << Global.functionLocations[calleeFid]->name << "\n";

    addEdge(uEntry, Edge::Type::CALL_EDGE, callSiteId, vEntry);
    addEdge(vExit, Edge::Type::RETURN_EDGE, callSiteId, uExit);
}

void ICFG::tryAndAddCallSite(int fid, const CFGBlock &B) {
    if (B.empty()) // block has no element
        return;

    std::optional<CFGStmt> CS = B.back().getAs<CFGStmt>();
    if (!CS) // the last element is not a CFGBlock
        return;

    const CallExpr *expr = dyn_cast<CallExpr>(CS->getStmt());
    if (!expr) // the last stmt is not a CallExpr
        return;

    const FunctionDecl *calleeDecl = expr->getDirectCallee();
    if (!calleeDecl)
        return;

    // callsite block has only one successor
    requireTrue(B.succ_size() == 1);
    const CFGBlock &Succ = **B.succ_begin();

    // get node id of callsite & its successor
    int uEntry = getNodeId(fid, B.getBlockID());
    int uExit = getNodeId(fid, Succ.getBlockID());

    // get callee （这边没法提供 callee 函数定义所在的文件名）
    std::string callee = getFullSignature(calleeDecl);
    int calleeFid = Global.getIdOfFunction(callee);
    if (calleeFid == -1) {
        // callee has yet to be added to this ICFG
        requireTrue(!visited(calleeFid));
        callsitesToProcess[callee].push_back({uEntry, uExit});
    } else {
        requireTrue(visited(calleeFid));
        addCallAndReturnEdge(uEntry, uExit, calleeFid);
    }
}

void ICFG::addFunction(int fid, const CFG &cfg) {
    if (fid == -1 || visited(fid))
        return;

    n += cfg.size();
    G.resize(n);
    G_reverse.resize(n);

    entryExitOfFunction[fid] = {cfg.getEntry().getBlockID(),
                                cfg.getExit().getBlockID()};

    // if (cfg.size() <= 3)
    //     return;

    // add all blocks to graph
    // 先把所有的 CFGBlock 都加入到图中，这样顺序好点
    for (auto BI = cfg.begin(); BI != cfg.end(); ++BI) {
        const CFGBlock &B = **BI;
        getNodeId(fid, B.getBlockID());
    }

    // traverse each block
    for (auto BI = cfg.begin(); BI != cfg.end(); ++BI) {
        const CFGBlock &B = **BI;

        // traverse all successors to add normal edges
        for (auto SI = B.succ_begin(); SI != B.succ_end(); ++SI) {
            const CFGBlock *Succ = *SI;
            // Successor may be null in case of optimized-out edges. See:
            // https://clang.llvm.org/doxygen/classclang_1_1CFGBlock.html#details
            if (!Succ)
                continue;
            addNormalEdge(fid, B.getBlockID(), Succ->getBlockID());
        }

        tryAndAddCallSite(fid, B);
    }

    const std::string &signature = Global.functionLocations[fid].name;
    // process callsites
    for (const auto &[uEntry, uExit] : callsitesToProcess[signature]) {
        addCallAndReturnEdge(uEntry, uExit, fid);
    }
    callsitesToProcess.erase(signature);

    // cfg.viewCFG(LangOptions());
}
