#include "lib/adjust.hpp"

#include <cstdlib>
#include <sstream>
#include <vector>

namespace lib {

int64_t AdjustmentStore::get(TitleId id, const Uid& user) const {
    auto it = values_.find({id, user});
    return it == values_.end() ? 0 : it->second;
}

void AdjustmentStore::set(TitleId id, const Uid& user, int64_t seconds) {
    if (seconds == 0) {
        values_.erase({id, user});
    } else {
        values_[{id, user}] = seconds;
    }
}

std::string AdjustmentStore::serialize() const {
    std::ostringstream out;
    out << "# Library playtime adjustments (seconds)\n";
    for (const auto& kv : values_) {
        out << "adjust|" << titleHex(kv.first.first) << "|" << kv.first.second.hex() << "|" << kv.second << "\n";
    }
    return out.str();
}

AdjustmentStore AdjustmentStore::parse(const std::string& text) {
    AdjustmentStore store;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> p;
        std::string cur;
        for (char c : line) {
            if (c == '|') { p.push_back(cur); cur.clear(); } else cur += c;
        }
        p.push_back(cur);
        if (p.size() < 4 || p[0] != "adjust") continue;
        TitleId id;
        Uid uid;
        char* end = nullptr;
        const long long seconds = std::strtoll(p[3].c_str(), &end, 10);
        if (parseTitleId(p[1], id) && Uid::parse(p[2], uid) && end && *end == '\0' && !p[3].empty()) {
            store.set(id, uid, seconds);
        }
    }
    return store;
}

}  // namespace lib
