#pragma once

#include "engine/data_dict.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace openads::engine {

// Fast in-memory lookup: "is this table published?" and "which articles
// reference it?" Built by reloading from DataDict after DD mutations.
class ReplCatalog {
public:
    void reload(const DataDict& dd);

    bool table_is_published(const std::string& table_alias) const;

    std::vector<DataDict::ArticleEntry>
        articles_for_table(const std::string& table_alias) const;

private:
    // table_alias -> list of articles referencing it (case-insensitive keys)
    std::unordered_map<std::string, std::vector<DataDict::ArticleEntry>>
        articles_by_table_;
};

} // namespace openads::engine
