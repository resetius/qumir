#pragma once

#include <cstdint>
#include <vector>
#include <map>
#include <unordered_map>

#include <qumir/parser/type.h>

namespace NQumir {
namespace NIR {

enum class EKind : uint8_t {
    I1,
    I8, // for char* (strings)
    I16, // unused
    I32, // unused
    I64,
    U8, // unused
    U16, // unused
    U32, // unused
    U64, // unused
    F32, // unused
    F64,
    Void,
    Undef, // bad type for optimization errors
    Ptr,
    Func,
    Struct
};

struct TType {
    EKind Kind;
    // Pointer: Kind=Ptr, Aux=points to TType of the pointee
    // Function: Kind=Func, Aux=points to funSigId
    // Struct: Kind=Struct, Aux=points to structId
    int Aux=-1;
};

struct TFuncSig {
    std::vector<int> Params;
    int Result;
};

struct TStructL {
    std::vector<int> FieldTypes; /* TODO: align/packed/hasVptr */
};

class TTypeTable {
public:
    int I(EKind k);
    int Ptr(int to);
    int Func(std::vector<int> args, int ret);
    int Struct(std::vector<int> fields);
    int Unify(int left, int right);

    void Print(std::ostream& out, int typeId) const;
    void Format(std::ostream& out, uint64_t bitRepr, int typeId) const;

    bool IsPrimitive(int typeId) const;
    bool IsFloat(int typeId) const;
    bool IsInteger(int typeId) const;
    bool IsVoid(int typeId) const;
    bool IsPointer(int typeId) const;
    EKind GetKind(int typeId) const;

private:
    std::vector<TType> Types;
    std::vector<TFuncSig> FuncSigs;
    std::vector<TStructL> Structs;

    std::unordered_map<EKind,int> PrimitiveCache;
    std::unordered_map<int, int> PtrCache;
    std::map<std::tuple<std::vector<int>,int>, int> FuncCache;
    std::map<std::vector<int>, int> StructCache;
};

int FromAstType(const NAst::TTypePtr& tastType, TTypeTable& tt);

} // namespace NIR
} // namespace NQumir
