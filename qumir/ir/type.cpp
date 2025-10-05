#include "type.h"

#include <iostream>
#include <sstream>
#include <iomanip>

namespace NQumir {
namespace NIR {

int TTypeTable::I(EKind k) {
    auto it = PrimitiveCache.find(k);
    if (it != PrimitiveCache.end()) return it->second;

    Types.push_back({
        .Kind = k,
        .Aux = -1
    });

    return PrimitiveCache[k] = (int)Types.size()-1;
}

int TTypeTable::Ptr(int to) {
    auto it = PtrCache.find(to);
    if (it != PtrCache.end()) return it->second;

    Types.push_back({
        .Kind = EKind::Ptr,
        .Aux = to
    });

    return PtrCache[to] = (int)Types.size()-1;
}

int TTypeTable::Func(std::vector<int> args, int ret) {
    auto it = FuncCache.find({args, ret});
    if (it != FuncCache.end()) return it->second;

    int id = (int)FuncSigs.size();
    FuncSigs.push_back({
        .Params = std::move(args),
        .Result = ret
    });
    Types.push_back({
        .Kind = EKind::Func,
        .Aux = id
    });

    return FuncCache[{std::move(args), ret}] = (int)Types.size()-1;
}

int TTypeTable::Struct(std::vector<int> fields) {
    auto it = StructCache.find(fields);
    if (it != StructCache.end()) return it->second;

    int id = (int)Structs.size();
    Structs.push_back({
        .FieldTypes = std::move(fields)
    });
    Types.push_back({
        .Kind = EKind::Struct,
        .Aux = id
    });

    return StructCache[std::move(fields)] = (int)Types.size()-1;
}

int TTypeTable::Unify(int leftId, int rightId) {
    if (leftId == rightId) return leftId;
    auto left = Types[leftId];
    auto right = Types[rightId];

    if (IsInteger(leftId) && IsInteger(rightId)) {
        // both integers: unify to the larger one, signedness rules:
        // if one is signed and the other unsigned, unify to signed
        throw std::runtime_error("Integer unification not implemented yet");
    }
    if (IsFloat(leftId) && IsFloat(rightId)) {
        // both floats: promote to larger
        throw std::runtime_error("Float unification not implemented yet");
    }
    if (IsInteger(leftId) && IsFloat(rightId)) {
        // int to float: promote to float
        return rightId;
    }
    if (IsFloat(leftId) && IsInteger(rightId)) {
        // float to int: promote to float
        return leftId;
    }
    auto leftKind = left.Kind;
    auto rightKind = right.Kind;
    throw std::runtime_error("Cannot unify types of different kinds: " + std::to_string((int)leftKind) + " and " + std::to_string((int)rightKind) + " ids: " + std::to_string(leftId) + " and " + std::to_string(rightId) + ")");
}

int FromAstType(const NAst::TTypePtr& t, TTypeTable& tt) {
    if (auto i = NAst::TMaybeType<NAst::TIntegerType>(t)) {
        return tt.I(EKind::I64);
    }
    if (auto f = NAst::TMaybeType<NAst::TFloatType>(t)) {
        return tt.I(EKind::F64);
    }

    if (NAst::TMaybeType<NAst::TBoolType>(t)) {
        return tt.I(EKind::I1);
    }
    if (NAst::TMaybeType<NAst::TVoidType>(t)) {
        return tt.I(EKind::Void);
    }

    if (auto p = NAst::TMaybeType<NAst::TPointerType>(t)) {
        int to = FromAstType(p.Cast()->PointeeType, tt);
        return tt.Ptr(to);
    }

    /*
    if (auto s = NAst::TMaybeType<NAst::TStructType>(t)) {
        std::vector<int> fs;
        for (auto& fld : s.Cast()->Fields) {
            fs.push_back(FromAstType(fld.Type, tt));
        }
        return tt.Struct(std::move(fs));
    }
    */

    if (auto fn = NAst::TMaybeType<NAst::TFunctionType>(t)) {
        std::vector<int> ps;
        for (auto& a : fn.Cast()->ParamTypes) {
            ps.push_back(FromAstType(a, tt));
        }
        int r = FromAstType(fn.Cast()->ReturnType, tt);
        return tt.Func(std::move(ps), r);
    }

    return -1;
}

void TTypeTable::Print(std::ostream& out, int typeId) const {
    if (typeId < 0 || typeId >= (int)Types.size()) {
        out << "<invalid type>";
        return;
    }
    auto& type = Types[typeId];
    switch (type.Kind) {
        case EKind::I1: out << "i1"; break;
        case EKind::I64: out << "i64"; break;
        case EKind::F64: out << "f64"; break;
        case EKind::Void: out << "void"; break;
        case EKind::Ptr: {
            out << "ptr to ";
            Print(out, type.Aux);
            break;
        }
        case EKind::Func: {
            auto& sig = FuncSigs[type.Aux];
            out << "func(";
            for (size_t i = 0; i < sig.Params.size(); ++i) {
                Print(out, sig.Params[i]);
                if (i < sig.Params.size() - 1) out << ", ";
            }
            out << ") -> ";
            Print(out, sig.Result);
            break;
        }
        case EKind::Struct: {
            auto& str = Structs[type.Aux];
            out << "struct { ";
            for (size_t i = 0; i < str.FieldTypes.size(); ++i) {
                Print(out, str.FieldTypes[i]);
                if (i < str.FieldTypes.size() - 1) out << "; ";
            }
            out << "}";
            break;
        }
    }
}

void TTypeTable::Format(std::ostream& out, uint64_t bitRepr, int typeId) const {
    if (typeId < 0 || typeId >= (int)Types.size()) {
        out << "<invalid type>";
        return;
    }
    auto& type = Types[typeId];
    std::ostringstream ss;
    switch (type.Kind) {
        case EKind::I1: {
            ss << (bitRepr ? "true" : "false");
            break;
        }
        case EKind::I64: {
            ss << (int64_t)bitRepr;
            break;
        }
        case EKind::F64: {
            double d = std::bit_cast<double>(bitRepr);
            ss << std::fixed << std::setprecision(15) << d;
            break;
        }
        case EKind::Void: {
            ss << "<void>";
            break;
        }
        case EKind::Ptr: {
            if (bitRepr == 0) {
                ss << "null";
            } else {
                ss << "0x" << std::hex << bitRepr << std::dec;
            }
            break;
        }
        case EKind::Func: {
            if (bitRepr == 0) {
                ss << "null";
            } else {
                ss << "<func 0x" << std::hex << bitRepr << std::dec << ">";
            }
            break;
        }
        case EKind::Struct: {
            ss << "<struct 0x" << std::hex << bitRepr << std::dec << ">";
            break;
        }
    }
    out << ss.str();
}

bool TTypeTable::IsVoid(int typeId) const {
    if (typeId < 0 || typeId >= (int)Types.size()) return false;
    auto k = Types[typeId].Kind;
    return k == EKind::Void;
}

bool TTypeTable::IsPrimitive(int typeId) const {
    if (typeId < 0 || typeId >= (int)Types.size()) return false;
    auto k = Types[typeId].Kind;
    return k != EKind::Ptr && k != EKind::Func && k != EKind::Struct;
}

bool TTypeTable::IsFloat(int typeId) const {
    if (typeId < 0 || typeId >= (int)Types.size()) return false;
    auto k = Types[typeId].Kind;
    return k == EKind::F64;
}

bool TTypeTable::IsInteger(int typeId) const {
    if (typeId < 0 || typeId >= (int)Types.size()) return false;
    auto k = Types[typeId].Kind;
    return k == EKind::I1 || k == EKind::I64;
}

EKind TTypeTable::GetKind(int typeId) const {
    if (typeId < 0 || typeId >= (int)Types.size()) {
        throw std::runtime_error("Invalid typeId in GetKind");
    }
    return Types[typeId].Kind;
}


} // namespace NIR
} // namespace NQumir
