#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <coroio/all.hpp>
#include <coroio/http/httpd.hpp>
#include <coroio/pipe/pipe.hpp>

namespace NQumir::NService {

using THandler = std::function<NNet::TFuture<void>(NNet::TRequest&, NNet::TResponse&)>;

// Everything a plugin gets from the host to serve its own endpoints.
struct TPluginContext {
    std::function<NNet::TPipe(const std::string&, const std::vector<std::string>&, bool)> PipeFactory;
    std::string BinaryDir;
    std::vector<std::string> Args;
};

// Built-in routes are matched first; a plugin can only add paths, never shadow them.
class TRouteTable {
public:
    void Get(std::string path, THandler handler) {
        Gets.emplace(std::move(path), std::move(handler));
    }

    void Post(std::string path, THandler handler) {
        Posts.emplace(std::move(path), std::move(handler));
    }

    const THandler* FindGet(const std::string& path) const {
        return Find(Gets, path);
    }

    const THandler* FindPost(const std::string& path) const {
        return Find(Posts, path);
    }

private:
    using TMap = std::unordered_map<std::string, THandler>;

    static const THandler* Find(const TMap& map, const std::string& path) {
        auto it = map.find(path);
        return it == map.end() ? nullptr : &it->second;
    }

    TMap Gets;
    TMap Posts;
};

} // namespace NQumir::NService

extern "C" void QumirPluginRegister(NQumir::NService::TRouteTable& routes,
                                    const NQumir::NService::TPluginContext& context);
