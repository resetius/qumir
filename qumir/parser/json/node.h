#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace NQumir {
namespace NAst {
namespace NJson {

enum class EType {
    String,
    Float,
    Integer,
    Boolean,
    Null,
    Object,
    Array,
};

struct IValue {
    virtual EType ValueType() const = 0;

    template<typename T>
    T* As() {
        if (ValueType() == T::Type) {
            return static_cast<T*>(this);
        }
        return nullptr;
    }

protected:
    friend struct TJson;

    IValue() = default;
    virtual ~IValue() = default;
};

struct TString : public IValue {
    static constexpr EType Type = EType::String;
    std::string Value;

    TString() = default;
    TString(std::string v)
        : Value(std::move(v))
    { }
    EType ValueType() const override { return Type; }
};

struct TFloat : public IValue {
    static constexpr EType Type = EType::Float;
    double Value;

    TFloat() = default;
    TFloat(double v)
        : Value(v)
    { }
    EType ValueType() const override { return Type; }
};

struct TInteger : public IValue {
    static constexpr EType Type = EType::Integer;
    int64_t Value;

    TInteger() = default;
    TInteger(int64_t v)
        : Value(v)
    { }
    EType ValueType() const override { return Type; }
};

struct TBoolean : public IValue {
    static constexpr EType Type = EType::Boolean;
    bool Value;

    TBoolean() = default;
    TBoolean(bool v)
        : Value(v)
    { }
    EType ValueType() const override { return Type; }
};

struct TNull : public IValue {
    static constexpr EType Type = EType::Null;

    EType ValueType() const override { return Type; }
};

struct TArray : public IValue {
    static constexpr EType Type = EType::Array;
    std::vector<IValue*> Elements;

    EType ValueType() const override { return Type; }
};

struct TObject : public IValue {
    static constexpr EType Type = EType::Object;

    // assume that keys are sorted
    using TMember = std::pair<std::string, IValue*>;
    using TMemberList = std::vector<TMember>;
    TMemberList Members;

    IValue* Get(const std::string& key) const;
    std::pair<TMemberList::const_iterator, TMemberList::const_iterator> GetRange(const std::string& key) const;
    EType ValueType() const override { return Type; }
};

struct TJson {
    TJson() = default;
    ~TJson();

    TJson(const TJson&) = delete;
    TJson& operator=(const TJson&) = delete;
    TJson(TJson&& other) noexcept
        : Nodes(std::move(other.Nodes))
    {
        other.Nodes.clear();
    }
    TJson& operator=(TJson&& other) noexcept {
        if (this != &other) {
            Nodes = std::move(other.Nodes);
            other.Nodes.clear();
        }
        return *this;
    }

    IValue* Root() const {
        if (Nodes.empty()) {
            return nullptr;
        }
        return Nodes.front();
    }

    template<typename T, typename... Args>
    T* NewNode(Args&&... args) {
        T* node = new T(std::forward<Args>(args)...);
        Nodes.emplace_back(node);
        return node;
    }

private:
    std::vector<IValue*> Nodes;
};

} // namespace NJson
} // namespace NAst
} // namespace NQumir
