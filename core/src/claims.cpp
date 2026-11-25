#include "lib/claims.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace lib {
namespace {

std::string clean(std::string s) {
    for (char& c : s) {
        if (c == '|' || c == ',' || c == '\n' || c == '\r') {
            c = ' ';
        }
    }
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) b++;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) e--;
    return s.substr(b, e - b);
}

bool hasUser(const ClaimEntry& e, const Uid& u) {
    return std::find(e.users.begin(), e.users.end(), u) != e.users.end();
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            parts.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    parts.push_back(cur);
    return parts;
}

}  // namespace

std::string normalizeName(const std::string& s) {
    std::string out;
    bool space = false;
    for (unsigned char c : s) {
        if (std::isspace(c)) {
            space = true;
            continue;
        }
        if (space && !out.empty()) {
            out += ' ';
        }
        space = false;
        out += (c < 0x80) ? static_cast<char>(std::tolower(c)) : static_cast<char>(c);
    }
    return out;
}

const ClaimEntry* ClaimStore::find(TitleId id) const {
    for (const ClaimEntry& e : entries_) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

ClaimEntry* ClaimStore::find(TitleId id) {
    return const_cast<ClaimEntry*>(static_cast<const ClaimStore*>(this)->find(id));
}

std::vector<const ClaimEntry*> ClaimStore::visibleEntries(const Uid& u) const {
    std::vector<const ClaimEntry*> vis;
    for (const ClaimEntry& e : entries_) {
        if (hasUser(e, u)) vis.push_back(&e);
    }
    return vis;
}

bool ClaimStore::isClaimed(TitleId id, const Uid& u) const {
    const ClaimEntry* e = find(id);
    return e != nullptr && hasUser(*e, u);
}

bool ClaimStore::isClaimedByAnyone(TitleId id) const {
    return find(id) != nullptr;
}

bool ClaimStore::isAlias(TitleId id, const Uid& u) const {
    const ClaimEntry* e = find(id);
    if (e == nullptr || !hasUser(*e, u)) return false;
    std::string n = normalizeName(e->name);
    if (n.empty()) return false;
    for (const ClaimEntry* o : visibleEntries(u)) {
        if (o == e) return false;
        if (normalizeName(o->name) == n) return true;
    }
    return false;
}

std::vector<TitleId> ClaimStore::visible(const Uid& u) const {
    std::vector<TitleId> ids;
    for (const ClaimEntry* e : visibleEntries(u)) ids.push_back(e->id);
    return ids;
}

std::vector<TitleId> ClaimStore::group(TitleId id, const Uid& u) const {
    std::vector<TitleId> members;
    const ClaimEntry* e = find(id);
    if (e == nullptr || !hasUser(*e, u) || isAlias(id, u)) return members;

    std::string n = normalizeName(e->name);
    for (const ClaimEntry* o : visibleEntries(u)) {
        if (o == e || (!n.empty() && normalizeName(o->name) == n)) members.push_back(o->id);
    }
    return members;
}

void ClaimStore::add(TitleId id, const std::string& name, const Uid& u) {
    ClaimEntry* e = find(id);
    if (e == nullptr) {
        ClaimEntry n;
        n.id = id;
        n.name = clean(name);
        n.users.push_back(u);
        entries_.push_back(std::move(n));
    } else {
        if (!hasUser(*e, u)) e->users.push_back(u);
        if (e->name.empty()) e->name = clean(name);
    }
}

void ClaimStore::remove(TitleId id, const Uid& u) {
    ClaimEntry* e = find(id);
    if (e == nullptr) return;
    e->users.erase(std::remove(e->users.begin(), e->users.end(), u), e->users.end());
    if (e->users.empty()) {
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [id](const ClaimEntry& c) { return c.id == id; }), entries_.end());
    }
}

bool ClaimStore::isIgnored(TitleId id, const Uid& u) const {
    return std::find(ignored_.begin(), ignored_.end(), std::make_pair(id, u)) != ignored_.end();
}

void ClaimStore::ignore(TitleId id, const Uid& u) {
    if (!isIgnored(id, u)) ignored_.emplace_back(id, u);
}

bool ClaimStore::updateSnapshot(TitleId id, const std::string& name, const std::optional<SystemStats>& live) {
    ClaimEntry* e = find(id);
    if (e == nullptr) return false;

    bool changed = false;
    std::string n = clean(name);
    if (!n.empty() && n != e->name) {
        e->name = n;
        changed = true;
    }

    if (live && (live->playSeconds > 0 || live->launches > 0)) {
        SystemStats merged = e->hasSnapshot ? e->snapshot : SystemStats{};
        merged.playSeconds = std::max(merged.playSeconds, live->playSeconds);
        merged.launches = std::max(merged.launches, live->launches);
        if (live->first != 0 && (merged.first == 0 || live->first < merged.first)) merged.first = live->first;
        merged.last = std::max(merged.last, live->last);

        if (!e->hasSnapshot || merged.playSeconds != e->snapshot.playSeconds || merged.launches != e->snapshot.launches ||
            merged.first != e->snapshot.first || merged.last != e->snapshot.last) {
            e->snapshot = merged;
            e->hasSnapshot = true;
            changed = true;
        }
    }
    return changed;
}

std::string ClaimStore::serialize() const {
    std::ostringstream out;
    out << "# Library claims\n";
    for (const ClaimEntry& e : entries_) {
        out << "claim|" << titleHex(e.id) << "|" << clean(e.name) << "|" << (e.hasSnapshot ? 1 : 0) << "|" << e.snapshot.playSeconds
            << "|" << e.snapshot.launches << "|" << e.snapshot.first << "|" << e.snapshot.last << "|";
        for (size_t i = 0; i < e.users.size(); i++) out << (i ? "," : "") << e.users[i].hex();
        out << "\n";
    }
    for (const auto& ig : ignored_) out << "ignore|" << titleHex(ig.first) << "|" << ig.second.hex() << "\n";
    return out.str();
}

ClaimStore ClaimStore::parse(const std::string& text) {
    ClaimStore store;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> p = split(line, '|');
        if (p.size() >= 9 && p[0] == "claim") {
            ClaimEntry e;
            if (!parseTitleId(p[1], e.id) || store.find(e.id)) continue;
            e.name = p[2];
            e.hasSnapshot = p[3] == "1";
            e.snapshot.playSeconds = std::strtoll(p[4].c_str(), nullptr, 10);
            e.snapshot.launches = static_cast<uint32_t>(std::strtoul(p[5].c_str(), nullptr, 10));
            e.snapshot.first = std::strtoll(p[6].c_str(), nullptr, 10);
            e.snapshot.last = std::strtoll(p[7].c_str(), nullptr, 10);
            for (const std::string& u : split(p[8], ',')) {
                Uid uid;
                if (Uid::parse(u, uid)) e.users.push_back(uid);
            }
            if (!e.users.empty()) store.entries_.push_back(std::move(e));
        } else if (p.size() >= 3 && p[0] == "ignore") {
            TitleId id;
            Uid uid;
            if (parseTitleId(p[1], id) && Uid::parse(p[2], uid)) store.ignore(id, uid);
        }
    }
    return store;
}

}  // namespace lib
