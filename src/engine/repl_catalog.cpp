#include "engine/repl_catalog.h"

#include <algorithm>
#include <cctype>

namespace openads::engine {

namespace {

std::string to_lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

void ReplCatalog::reload(const DataDict& dd) {
    articles_by_table_.clear();
    for (auto& kv : dd.articles()) {
        const auto& a = kv.second;
        if (!a.enabled) continue;
        std::string key = to_lower(a.source_table);
        articles_by_table_[key].push_back(a);
    }
}

bool ReplCatalog::table_is_published(const std::string& table_alias) const {
    return articles_by_table_.count(to_lower(table_alias)) > 0;
}

std::vector<DataDict::ArticleEntry>
ReplCatalog::articles_for_table(const std::string& table_alias) const {
    auto it = articles_by_table_.find(to_lower(table_alias));
    if (it == articles_by_table_.end()) return {};
    return it->second;
}

} // namespace openads::engine
