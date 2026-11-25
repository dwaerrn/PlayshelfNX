#include "lib/history.hpp"

#include <cstdlib>
#include <map>
#include <sstream>
#include <tuple>

namespace lib {
namespace {

using Key = std::tuple<int, TitleId, uint64_t, uint64_t, int64_t, int64_t>;

Key keyOf(const RawEvent& e) {
    return Key(static_cast<int>(e.kind), e.title, e.user.hi, e.user.lo, e.clock, e.steady);
}

}  // namespace

size_t HistoryStore::merge(const std::vector<RawEvent>& fresh) {
    std::map<Key, int> existing;
    for (const RawEvent& e : events_) existing[keyOf(e)]++;

    size_t added = 0;
    for (const RawEvent& e : fresh) {
        auto it = existing.find(keyOf(e));
        if (it != existing.end() && it->second > 0) {
            it->second--;   // already stored
            continue;
        }
        events_.push_back(e);
        added++;
    }
    return added;
}

std::string HistoryStore::serialize() const {
    std::ostringstream out;
    out << "# Library history v1\n";
    for (const RawEvent& e : events_) {
        out << static_cast<int>(e.kind) << "|" << titleHex(e.title) << "|" << (e.user.valid() ? e.user.hex() : "-") << "|" << e.clock << "|" << e.steady << "\n";
    }
    return out.str();
}

HistoryStore HistoryStore::parse(const std::string& text) {
    HistoryStore store;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> p;
        std::string cur;
        for (char c : line) {
            if (c == '|') { p.push_back(cur); cur.clear(); } else if (c != '\r') cur += c;
        }
        p.push_back(cur);
        if (p.size() < 5) continue;

        RawEvent e;
        int kind = std::atoi(p[0].c_str());
        if (kind < 0 || kind > static_cast<int>(EventKind::AccountInactive)) continue;
        e.kind = static_cast<EventKind>(kind);
        if (!parseTitleId(p[1], e.title)) continue;
        if (p[2] != "-") Uid::parse(p[2], e.user);
        e.clock = std::strtoll(p[3].c_str(), nullptr, 10);
        e.steady = std::strtoll(p[4].c_str(), nullptr, 10);
        store.events_.push_back(e);
    }
    return store;
}

void NameCache::update(const std::vector<TitleInfo>& titles) {
    for (const TitleInfo& t : titles) {
        if (t.name.empty()) continue;
        std::string name = t.name;
        for (char& c : name) if (c == '|' || c == '\n' || c == '\r') c = ' ';   // keep the file format intact
        bool found = false;
        for (auto& n : names_) {
            if (n.first == t.id) { n.second = name; found = true; break; }
        }
        if (!found) names_.emplace_back(t.id, name);
    }
}

std::string NameCache::name(TitleId id) const {
    for (const auto& n : names_) {
        if (n.first == id) return n.second;
    }
    return "";
}

std::string NameCache::serialize() const {
    std::ostringstream out;
    out << "# Library names v1\n";
    for (const auto& n : names_) {
        std::string clean = n.second;
        for (char& c : clean) if (c == '|' || c == '\n' || c == '\r') c = ' ';
        out << titleHex(n.first) << "|" << clean << "\n";
    }
    return out.str();
}

NameCache NameCache::parse(const std::string& text) {
    NameCache cache;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t bar = line.find('|');
        TitleId id;
        if (bar == std::string::npos || !parseTitleId(line.substr(0, bar), id)) continue;
        cache.names_.emplace_back(id, line.substr(bar + 1));
    }
    return cache;
}

}  // namespace lib
