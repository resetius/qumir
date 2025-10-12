#pragma once

#include "location.h"

#include <list>
#include <string>
#include <exception>

namespace NQumir {

class TError : public std::exception {
public:
    TError(TLocation loc, const std::string& message)
        : Location(loc)
        , Msg(message)
    { }

    TError(TLocation loc, const std::exception& ex)
        : Location(loc)
    {
        if (auto e = dynamic_cast<const TError*>(&ex)) {
            // If wrapping an existing parser error at the same location with no message,
            // flatten to avoid duplicate empty frames.
            if (e->Location.Line == Location.Line && e->Location.Column == Location.Column && e->Msg.empty()) {
                Msg = e->Msg; // likely empty
                Children = e->Children;
            } else {
                Children.push_back(*e);
            }
        } else {
            // For generic exceptions, store the message directly instead of nesting.
            Msg = ex.what();
        }
    }

    TError(const std::exception& ex)
        : TError({}, ex)
    { }

    TError(TLocation loc, const std::list<TError>& children)
        : Location(loc)
        , Children(children)
    { }

    const char* what() const noexcept override { return Msg.c_str(); }

    std::string ToString() const;

private:
    std::string ToString(int indent) const;

    std::string Msg;
    TLocation Location;
    std::list<TError> Children;
};

} // namespace NQumir