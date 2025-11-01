#pragma once

#include <memory>
#include <vector>
#include <optional>

namespace NQumir {
namespace NAst {

struct TType {
    bool Mutable = true;

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

    // Only 1 integer type
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

std::ostream& operator<<(std::ostream& os, const TType& expr);

} // namespace NAst
} // namespace NQumir