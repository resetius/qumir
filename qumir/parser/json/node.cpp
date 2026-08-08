#include "node.h"

#include <algorithm>

namespace NQumir {
namespace NAst {
namespace NJson {

TJson::~TJson() {
    for (auto* node : Nodes) {
        delete node;
    }
}

IValue* TObject::Get(const std::string& key) const {
    auto it = std::lower_bound(Members.begin(), Members.end(), key, [](const auto& pair, const std::string& k) {
        return pair.first < k;
    });
    if (it != Members.end() && it->first == key) {
        return it->second;
    }
    return nullptr;
}

std::pair<TObject::TMemberList::const_iterator, TObject::TMemberList::const_iterator> TObject::GetRange(const std::string& key) const {
    auto first = std::lower_bound(Members.begin(), Members.end(), key, [](const auto& member, const std::string& key) {
        return member.first < key;
    });
    auto last = std::upper_bound(Members.begin(), Members.end(), key, [](const std::string& k, const auto& pair) {
        return k < pair.first;
    });

    return std::make_pair(first, last);
}

} // namespace NJson
} // namespace NAst
} // namespace NQumir
