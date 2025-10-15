#include "locals2ssa.h"

namespace NQumir {
namespace NIR {

using namespace NLiterals;

namespace NPasses {

namespace {

struct PhiInfo {
    int Local;
    int DstTmp;
    std::vector<int> ArgTmp; // size = number of predecessors
};

struct SealedSSATables {
    // local -> (block -> tmp)
    std::unordered_map<int, std::unordered_map<int, int>> CurrentDef;
    std::unordered_map<int, std::vector<PhiInfo>> IncompletePhis;
};

void WriteVariable(SealedSSATables& T, int localIdx, int blockIdx, int valueTmp) {
    T.CurrentDef[localIdx][blockIdx] = valueTmp;
}

bool HasCurrent(const SealedSSATables& T, int localIdx, int blockIdx) {
    auto it = T.CurrentDef.find(localIdx);
    if (it==T.CurrentDef.end()) return false;
    return it->second.find(blockIdx)!=it->second.end();
}

void MaterializePhiInstr(TBlock* B, const PhiInfo& ph) {
    TInstr I { "phi"_op };
    I.Dest = TTmp{ ph.DstTmp };
    // Args: [ (pred0, arg0), (pred1, arg1), ... ]
    auto capacity = I.Operands.size();
    if (ph.ArgTmp.size() * 2 != capacity) {
        throw std::runtime_error("Too many phi arguments");
    }
    auto pred = B->Pred.begin();
    for (size_t i = 0; i < ph.ArgTmp.size() && i < capacity; ++i) {
        I.Operands[2*i] = TLabel{pred->Idx};
        I.Operands[2*i+1] = TTmp{ph.ArgTmp[i]};
        ++pred;
    }
    B->Phis.push_back(I);
}

} // anonymous namespace

void PromoteLocalsToSSA(TFunction& function, TModule& module)
{

}

void PromoteLocalsToSSA(TModule& module) {
    for (auto& function : module.Functions) {
        PromoteLocalsToSSA(function, module);
    }
}

} // namespace NPasses
} // namespace NIR
} // namespace NQumir