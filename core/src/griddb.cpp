#include "lib/griddb.hpp"

#include <cstdlib>
#include <memory>
#include <sstream>

namespace lib::griddb {
namespace {

// Small JSON reader: enough for the API's answers
struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> fields;

    const Json* get(const std::string& key) const {
        for (const auto& f : fields) {
            if (f.first == key) return &f.second;
        }
        return nullptr;
    }
    std::string text(const std::string& key) const {
        const Json* v = get(key);
        return v && v->type == String ? v->str : "";
    }
    double number(const std::string& key) const {
        const Json* v = get(key);
        return v && v->type == Number ? v->num : 0;
    }
};

class Parser {
  public:
    explicit Parser(const std::string& s) : s_(s) { }

    bool parse(Json& out) {
        if (!value(out, 0)) return false;
        space();
        return i_ == s_.size();
    }

  private:
    const std::string& s_;
    size_t i_ = 0;

    void space() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) i_++;
    }
    bool literal(const char* word) {
        size_t n = std::char_traits<char>::length(word);
        if (s_.compare(i_, n, word) != 0) return false;
        i_ += n;
        return true;
    }
    static void utf8(std::string& out, uint32_t c) {
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    bool hex4(uint32_t& v) {
        if (i_ + 4 > s_.size()) return false;
        v = 0;
        for (int k = 0; k < 4; k++) {
            char c = s_[i_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else return false;
        }
        return true;
    }
    bool string(std::string& out) {
        if (i_ >= s_.size() || s_[i_] != '"') return false;
        i_++;
        while (i_ < s_.size()) {
            char c = s_[i_++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (i_ >= s_.size()) return false;
            char e = s_[i_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t c1;
                    if (!hex4(c1)) return false;
                    if (c1 >= 0xD800 && c1 < 0xDC00 && s_.compare(i_, 2, "\\u") == 0) {
                        i_ += 2;
                        uint32_t c2;
                        if (!hex4(c2)) return false;
                        c1 = 0x10000 + ((c1 - 0xD800) << 10) + (c2 - 0xDC00);
                    }
                    utf8(out, c1);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }
    bool value(Json& out, int depth) {
        if (depth > 64) return false;
        space();
        if (i_ >= s_.size()) return false;
        char c = s_[i_];
        if (c == '{') {
            out.type = Json::Object;
            i_++;
            space();
            if (i_ < s_.size() && s_[i_] == '}') { i_++; return true; }
            while (true) {
                space();
                std::string key;
                if (!string(key)) return false;
                space();
                if (i_ >= s_.size() || s_[i_++] != ':') return false;
                Json v;
                if (!value(v, depth + 1)) return false;
                out.fields.emplace_back(std::move(key), std::move(v));
                space();
                if (i_ >= s_.size()) return false;
                if (s_[i_] == ',') { i_++; continue; }
                if (s_[i_] == '}') { i_++; return true; }
                return false;
            }
        }
        if (c == '[') {
            out.type = Json::Array;
            i_++;
            space();
            if (i_ < s_.size() && s_[i_] == ']') { i_++; return true; }
            while (true) {
                Json v;
                if (!value(v, depth + 1)) return false;
                out.items.push_back(std::move(v));
                space();
                if (i_ >= s_.size()) return false;
                if (s_[i_] == ',') { i_++; continue; }
                if (s_[i_] == ']') { i_++; return true; }
                return false;
            }
        }
        if (c == '"') {
            out.type = Json::String;
            return string(out.str);
        }
        if (literal("true")) { out.type = Json::Bool; out.b = true; return true; }
        if (literal("false")) { out.type = Json::Bool; return true; }
        if (literal("null")) { out.type = Json::Null; return true; }
        const char* start = s_.c_str() + i_;
        char* end = nullptr;
        out.num = std::strtod(start, &end);
        if (end == start) return false;
        out.type = Json::Number;
        i_ += static_cast<size_t>(end - start);
        return true;
    }
};

// Parses an API answer and returns its "data" array (nullptr unless success is true)
const Json* dataOf(const std::string& text, Json& root) {
    if (!Parser(text).parse(root) || root.type != Json::Object) return nullptr;
    const Json* ok = root.get("success");
    const Json* data = root.get("data");
    if (!ok || ok->type != Json::Bool || !ok->b || !data || data->type != Json::Array) return nullptr;
    return data;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(' ');
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(' ');
    return s.substr(a, b - a + 1);
}

}  // namespace

std::string cleanName(const std::string& name) {
    static const char* drop[] = {"\xE2\x84\xA2", "\xC2\xAE", "\xC2\xA9"};   // ™ ® ©
    std::string s = name;
    for (const char* d : drop) {
        size_t pos;
        while ((pos = s.find(d)) != std::string::npos) s.erase(pos, std::char_traits<char>::length(d));
    }
    std::string out;
    for (char c : s) {
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        if (c == ' ' && (out.empty() || out.back() == ' ')) continue;
        out += c;
    }
    return trim(out);
}

std::vector<std::string> searchTerms(const std::string& name) {
    std::vector<std::string> terms;
    const std::string full = cleanName(name);
    if (full.empty()) return terms;
    terms.push_back(full);
    size_t cut = std::string::npos;
    for (const char* sep : {":", " - "}) {
        size_t p = full.find(sep);
        if (p != std::string::npos && p < cut) cut = p;
    }
    if (cut != std::string::npos) {
        std::string shorter = trim(full.substr(0, cut));
        if (!shorter.empty() && shorter != full) terms.push_back(shorter);
    }
    return terms;
}

std::string urlEncode(const std::string& text) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::string searchUrl(const std::string& base, const std::string& term) {
    return base + "/search/autocomplete/" + urlEncode(term);
}

std::string gridsUrl(const std::string& base, int64_t gameId) {
    return base + "/grids/game/" + std::to_string(gameId) +
           "?dimensions=600x900,342x482,660x930&types=static&mimes=image/png,image/jpeg&nsfw=false&humor=false";
}

bool parseSearch(const std::string& json, std::vector<Game>& out) {
    out.clear();
    Json root;
    const Json* data = dataOf(json, root);
    if (!data) return false;
    for (const Json& item : data->items) {
        if (item.type != Json::Object) continue;
        Game g;
        g.id = static_cast<int64_t>(item.number("id"));
        g.name = item.text("name");
        if (g.id > 0) out.push_back(std::move(g));
    }
    return true;
}

bool parseGrids(const std::string& json, std::vector<Grid>& out) {
    out.clear();
    Json root;
    const Json* data = dataOf(json, root);
    if (!data) return false;
    for (const Json& item : data->items) {
        if (item.type != Json::Object) continue;
        Grid g;
        g.id = static_cast<int64_t>(item.number("id"));
        g.url = item.text("url");
        g.thumb = item.text("thumb");
        g.mime = item.text("mime");
        g.width = static_cast<int>(item.number("width"));
        g.height = static_cast<int>(item.number("height"));
        const bool usable = g.mime == "image/png" || g.mime == "image/jpeg";
        const bool portrait = g.width == 0 || g.height == 0 || g.height > g.width;
        if (!g.url.empty() && usable && portrait) out.push_back(std::move(g));
    }
    return true;
}

namespace {

std::string failure(const HttpResponse& resp, const char* what, Outcome& outcome) {
    if (resp.status == 401 || resp.status == 403) {
        outcome = Outcome::BadKey;
        return "the API key was refused";
    }
    outcome = Outcome::Error;
    return std::string(what) + ": " + (resp.status ? "HTTP " + std::to_string(resp.status) : resp.error);
}

}  // namespace

Outcome searchGames(HttpClient& http, const std::string& base, const std::string& key, const std::string& term,
                    std::vector<Game>& out, std::string& message) {
    out.clear();
    if (cleanName(term).empty()) return Outcome::NoMatch;
    HttpResponse resp = http.get(searchUrl(base, cleanName(term)), {"Authorization: Bearer " + key});
    if (resp.status == 404) return Outcome::NoMatch;    // nothing under that name
    Outcome o;
    if (resp.status != 200) {
        message = failure(resp, "search", o);
        return o;
    }
    if (!parseSearch(resp.body, out)) {
        message = "search: unexpected answer";
        return Outcome::Error;
    }
    return out.empty() ? Outcome::NoMatch : Outcome::Found;
}

Outcome listGrids(HttpClient& http, const std::string& base, const std::string& key, int64_t gameId,
                  std::vector<Grid>& out, std::string& message) {
    out.clear();
    HttpResponse resp = http.get(gridsUrl(base, gameId), {"Authorization: Bearer " + key});
    if (resp.status == 404) return Outcome::NoGrid;
    Outcome o;
    if (resp.status != 200) {
        message = failure(resp, "grids", o);
        return o;
    }
    if (!parseGrids(resp.body, out)) {
        message = "grids: unexpected answer";
        return Outcome::Error;
    }
    return out.empty() ? Outcome::NoGrid : Outcome::Found;
}

Outcome download(HttpClient& http, const std::string& url, std::string& out, std::string& message) {
    HttpResponse resp = http.get(url, {});
    if (resp.status != 200 || resp.body.empty()) {
        Outcome o;
        message = failure(resp, "image", o);
        out.clear();
        return o == Outcome::BadKey ? Outcome::Error : o;    // the CDN has no key to refuse
    }
    out = std::move(resp.body);
    return Outcome::Found;
}

Result fetchPoster(HttpClient& http, const std::string& base, const std::string& key, const std::string& name, int choice) {
    Result r;
    // 1) Find the game: the full name first, then a shorter form
    for (const std::string& term : searchTerms(name)) {
        std::vector<Game> games;
        Outcome o = searchGames(http, base, key, term, games, r.message);
        if (o == Outcome::Found) {
            r.gameId = games.front().id;
            r.gameName = games.front().name;
            break;
        }
        if (o != Outcome::NoMatch) {
            r.outcome = o;
            return r;
        }
    }
    if (r.gameId == 0) {
        r.outcome = Outcome::NoMatch;
        return r;
    }

    // 2) Its portrait posters, best rated first
    std::vector<Grid> grids;
    r.outcome = listGrids(http, base, key, r.gameId, grids, r.message);
    if (r.outcome != Outcome::Found) return r;

    // 3) The image itself (no key: it comes from the CDN)
    const Grid& g = grids[static_cast<size_t>(choice < 0 ? 0 : choice) % grids.size()];
    r.gridId = g.id;
    r.outcome = download(http, g.url, r.image, r.message);
    return r;
}

bool Memo::shouldFetch(TitleId id, int64_t now, int retryDays) const {
    auto it = entries_.find(id);
    if (it == entries_.end()) return true;
    const Entry& e = it->second;
    if (e.outcome == Outcome::Found || e.outcome == Outcome::Hidden || e.manual) return false;
    return now - e.time >= static_cast<int64_t>(retryDays) * 86400;
}

void Memo::record(TitleId id, Outcome outcome, int64_t gameId, int64_t now, const std::string& gameName, int64_t gridId, bool manual) {
    if (outcome == Outcome::Error || outcome == Outcome::BadKey) return;
    Entry e;
    e.outcome = outcome;
    e.gameId = gameId;
    e.time = now;
    e.gridId = gridId;
    e.manual = manual;
    for (char c : gameName) e.gameName += (c == '|' || c == '\n' || c == '\r') ? ' ' : c;
    entries_[id] = e;
}

void Memo::forget(TitleId id) {
    entries_.erase(id);
}

const Memo::Entry* Memo::find(TitleId id) const {
    auto it = entries_.find(id);
    return it == entries_.end() ? nullptr : &it->second;
}

std::string Memo::serialize() const {
    std::ostringstream out;
    out << "# Playshelf SteamGridDB lookups\n";
    for (const auto& kv : entries_) {
        const Entry& e = kv.second;
        const char* word = e.outcome == Outcome::Found     ? "found"
                           : e.outcome == Outcome::NoMatch ? "nomatch"
                           : e.outcome == Outcome::Hidden  ? "hidden"
                                                           : "nogrid";
        out << word << "|" << titleHex(kv.first) << "|" << e.gameId << "|" << e.time << "|" << e.gridId << "|" << (e.manual ? 1 : 0) << "|"
            << e.gameName << "\n";
    }
    return out.str();
}

Memo Memo::parse(const std::string& text) {
    Memo memo;
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
        if (p.size() < 4) continue;
        Outcome o;
        if (p[0] == "found") o = Outcome::Found;
        else if (p[0] == "nomatch") o = Outcome::NoMatch;
        else if (p[0] == "nogrid") o = Outcome::NoGrid;
        else if (p[0] == "hidden") o = Outcome::Hidden;
        else continue;
        TitleId id;
        if (!parseTitleId(p[1], id)) continue;
        Entry e;
        e.outcome = o;
        e.gameId = std::strtoll(p[2].c_str(), nullptr, 10);
        e.time = std::strtoll(p[3].c_str(), nullptr, 10);
        if (p.size() >= 7) {        // older files have only the first four fields
            e.gridId = std::strtoll(p[4].c_str(), nullptr, 10);
            e.manual = p[5] == "1";
            e.gameName = p[6];
        }
        memo.entries_[id] = e;
    }
    return memo;
}

}  // namespace lib::griddb
