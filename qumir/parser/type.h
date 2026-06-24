#pragma once

#include <memory>
#include <string>
#include <vector>
#include <optional>

namespace NQumir {
namespace NAst {

struct TType {
    bool Mutable = true;
    bool Readable = true;
    // Marks a TNamedType usage in a function parameter as a generic type
    // parameter (placeholder): the concrete type is determined per call site
    // from the argument's type, and the function gets instantiated for it.
    bool Template = false;

    virtual ~TType() = default;
    virtual std::string ToString() const { return ""; }
    virtual const std::string_view TypeName() const = 0;
};

using TTypePtr = std::shared_ptr<TType>;

template<typename T>
struct TMaybeType {
    TMaybeType(TTypePtr node)
        : Type(node && std::string_view(T::TypeId) == node->TypeName()
            ? std::static_pointer_cast<T>(std::move(node))
            : nullptr)
    { }

    std::shared_ptr<T> Cast() {
        return std::static_pointer_cast<T>(Type);
    }

    TTypePtr Type;
    operator bool() const { return Type != nullptr; }
};

struct TIntegerType : TType {
    static constexpr const char* TypeId = "Int";

    enum EKind {
        I8,
        I16,
        I32,
        I64,
        U8,
        U16,
        U32,
        U64,
    } Kind = I64;

    TIntegerType() = default;
    explicit TIntegerType(EKind kind)
        : Kind(kind)
    {}

    int BitWidth() const {
        switch (Kind) {
            case I8:
            case U8:
                return 8;
            case I16:
            case U16:
                return 16;
            case I32:
            case U32:
                return 32;
            case I64:
            case U64:
                return 64;
        }
        return 64;
    }

    bool IsSigned() const {
        switch (Kind) {
            case I8:
            case I16:
            case I32:
            case I64:
                return true;
            case U8:
            case U16:
            case U32:
            case U64:
                return false;
        }
        return true;
    }

    std::string ToString() const override {
        switch (Kind) {
            case I8: return "i8";
            case I16: return "i16";
            case I32: return "i32";
            case I64: return "i64";
            case U8: return "u8";
            case U16: return "u16";
            case U32: return "u32";
            case U64: return "u64";
        }
        return "i64";
    }

    const std::string_view TypeName() const override {
        return TIntegerType::TypeId;
    }
};

struct TFloatType : TType {
    static constexpr const char* TypeId = "Float";

    // Only 1 float type
    const std::string_view TypeName() const override {
        return TFloatType::TypeId;
    }
};

struct TBoolType : TType {
    static constexpr const char* TypeId = "Bool";

    TBoolType() = default;

    const std::string_view TypeName() const override {
        return TBoolType::TypeId;
    }
};

struct TStringType : TType {
    static constexpr const char* TypeId = "String";

    TStringType() = default;

    const std::string_view TypeName() const override {
        return TStringType::TypeId;
    }
};

struct TSymbolType : TType {
    static constexpr const char* TypeId = "Char";

    TSymbolType() = default;

    const std::string_view TypeName() const override {
        return TSymbolType::TypeId;
    }
};

struct TVoidType : TType {
    static constexpr const char* TypeId = "Void";

    TVoidType() = default;

    const std::string_view TypeName() const override {
        return TVoidType::TypeId;
    }
};

struct TFunctionType : TType {
    static constexpr const char* TypeId = "Fun";

    std::vector<TTypePtr> ParamTypes;
    TTypePtr ReturnType;

    TFunctionType(std::vector<TTypePtr> params, TTypePtr ret)
        : ParamTypes(std::move(params))
        , ReturnType(std::move(ret))
    {}

    std::string ToString() const override {
        std::string s = "(";
        for (size_t i = 0; i < ParamTypes.size(); ++i) {
            s += ParamTypes[i] ? ParamTypes[i]->TypeName() : "unknown";
            if (i < ParamTypes.size() - 1) {
                s += ", ";
            }
        }
        s += ") -> ";
        s += ReturnType ? ReturnType->TypeName() : "unknown";
        return s;
    }

    const std::string_view TypeName() const override {
        return TFunctionType::TypeId;
    }
};

struct TFutureType : TType {
    static constexpr const char* TypeId = "Future";

    TTypePtr ResultType;

    explicit TFutureType(TTypePtr result)
        : ResultType(std::move(result))
    {}

    std::string ToString() const override {
        return "Future<" + (ResultType ? ResultType->ToString() : std::string("unknown")) + ">";
    }

    const std::string_view TypeName() const override {
        return TFutureType::TypeId;
    }
};

struct TArrayType : TType {
    static constexpr const char* TypeId = "Array";

    TTypePtr ElementType;
    int Arity{0}; // 0 - scalar, 1 - 1D, 2 - 2D, etc.

    TArrayType() = default;

    explicit TArrayType(TTypePtr et, int arity)
        : ElementType(std::move(et))
        , Arity(arity)
    {}

    std::string ToString() const override {
        return "[" + (ElementType ? std::string(ElementType->TypeName()) : "unknown") + "; " +
               (Arity > 0 ? std::to_string(Arity) : "?") + "]";
    }

    const std::string_view TypeName() const override {
        return TArrayType::TypeId;
    }
};

struct TPointerType : TType {
    static constexpr const char* TypeId = "Ptr";

    TTypePtr PointeeType;

    explicit TPointerType(TTypePtr pt)
        : PointeeType(std::move(pt))
    {}

    std::string ToString() const override {
        return "*" + (PointeeType ? std::string(PointeeType->TypeName()) : "unknown");
    }

    const std::string_view TypeName() const override {
        return TPointerType::TypeId;
    }
};

struct TReferenceType : TType {
    static constexpr const char* TypeId = "Ref";

    TTypePtr ReferencedType;

    explicit TReferenceType(TTypePtr rt)
        : ReferencedType(std::move(rt))
    {}

    std::string ToString() const override {
        return "&" + (ReferencedType ? std::string(ReferencedType->TypeName()) : "unknown");
    }

    const std::string_view TypeName() const override {
        return TReferenceType::TypeId;
    }
};

struct TNamedType : TType {
    static constexpr const char* TypeId = "Named";
    std::string Name;
    TTypePtr UnderlyingType; // Resolved on name resolution phase, exported by modules
    std::optional<std::string> Reference; // Set = imported from this module; not printed in AST

    explicit TNamedType(std::string name, TTypePtr underlying)
        : Name(std::move(name))
        , UnderlyingType(std::move(underlying))
    {}

    std::string ToString() const override {
        if (UnderlyingType) {
            return Name + " (" + UnderlyingType->ToString() + ")";
        }
        return Name;
    }

    const std::string_view TypeName() const override {
        return TNamedType::TypeId; // "Named" — consistent with TMaybeType contract
    }
};

struct TStructType : TType {
    static constexpr const char* TypeId = "Struct";

    std::vector<std::pair<std::string, TTypePtr>> Fields;

    explicit TStructType(std::vector<std::pair<std::string, TTypePtr>> fields)
        : Fields(std::move(fields))
    {}

    std::string ToString() const override {
        std::string s = "struct { ";
        for (const auto& [name, type] : Fields) {
            s += name + ": " + (type ? std::string(type->TypeName()) : "unknown") + "; ";
        }
        s += "}";
        return s;
    }

    const std::string_view TypeName() const override {
        return TStructType::TypeId;
    }
};

std::ostream& operator<<(std::ostream& os, const TType& expr);

inline TTypePtr UnwrapReferenceType(TTypePtr type) {
    if (auto maybeRef = TMaybeType<TReferenceType>(type)) {
        return maybeRef.Cast()->ReferencedType;
    }
    return type;
}

inline TTypePtr UnwrapNamedType(TTypePtr type) {
    if (auto named = TMaybeType<TNamedType>(type)) {
        return named.Cast()->UnderlyingType;
    }
    return type;
}

inline bool IsFutureType(TTypePtr type) {
    return static_cast<bool>(TMaybeType<TFutureType>(type));
}

inline TTypePtr FutureResultType(TTypePtr type) {
    if (auto future = TMaybeType<TFutureType>(type)) {
        return future.Cast()->ResultType;
    }
    return nullptr;
}

inline TTypePtr WrapFutureType(TTypePtr type) {
    if (IsFutureType(type)) {
        return type;
    }
    return std::make_shared<TFutureType>(std::move(type));
}

// Stable string key for a type, used in cast/operator maps.
// Named types include their name to distinguish компл from цвет.
inline std::string TypeKey(const TTypePtr& t) {
    if (!t) return "unknown";
    if (auto named = TMaybeType<TNamedType>(t)) return std::string("Named::") + named.Cast()->Name;
    if (auto future = TMaybeType<TFutureType>(t)) return std::string("Future::") + TypeKey(future.Cast()->ResultType);
    return std::string(t->TypeName());
}

} // namespace NAst
} // namespace NQumir
