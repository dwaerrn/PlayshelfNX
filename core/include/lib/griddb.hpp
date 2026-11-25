#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "lib/types.hpp"

// SteamGridDB poster lookup by name (https://www.steamgriddb.com/api/v2). The network itself is
// behind HttpClient, so everything here runs on a PC and in the tests.
namespace lib::griddb {

constexpr const char* kApiBase = "https://www.steamgriddb.com/api/v2";

struct HttpResponse {
    int status = 0;             // 0: no answer (see error)
    std::string body;
    std::string error;
};

class HttpClient {
  public:
    virtual ~HttpClient() = default;
    // headers: complete lines such as "Authorization: Bearer ..."
    virtual HttpResponse get(const std::string& url, const std::vector<std::string>& headers) = 0;
};

struct Game {
    int64_t id = 0;
    std::string name;
};

struct Grid {
    int64_t id = 0;
    std::string url;
    std::string thumb;          // small preview (may be empty)
    std::string mime;
    int width = 0;
    int height = 0;
};

// Name used for the search: trademark signs removed, spaces collapsed
std::string cleanName(const std::string& name);
// Terms to try in order: the full name, then the part before ':' or " - " (if different)
std::vector<std::string> searchTerms(const std::string& name);
std::string urlEncode(const std::string& text);

std::string searchUrl(const std::string& base, const std::string& term);
std::string gridsUrl(const std::string& base, int64_t gameId);   // portrait posters, static, png/jpeg

// false if the text is not a successful API answer
bool parseSearch(const std::string& json, std::vector<Game>& out);
bool parseGrids(const std::string& json, std::vector<Grid>& out);

enum class Outcome {
    Found,      // image holds the downloaded file
    NoMatch,    // no game with that name
    NoGrid,     // game found, but it has no portrait poster
    BadKey,     // 401/403: stop asking
    Error,      // network or server trouble: try again next start
    Hidden,     // the user chose to show no downloaded poster (memo only)
};

struct Result {
    Outcome outcome = Outcome::Error;
    int64_t gameId = 0;
    std::string gameName;
    int64_t gridId = 0;
    std::string image;          // file contents (png or jpeg)
    std::string message;        // for the log on errors
};

// The single steps, for browsing. message explains Error / BadKey.
// searchGames: Found (out not empty) or NoMatch. listGrids: Found or NoGrid. download: Found.
Outcome searchGames(HttpClient& http, const std::string& base, const std::string& key, const std::string& term,
                    std::vector<Game>& out, std::string& message);
Outcome listGrids(HttpClient& http, const std::string& base, const std::string& key, int64_t gameId,
                  std::vector<Grid>& out, std::string& message);
Outcome download(HttpClient& http, const std::string& url, std::string& out, std::string& message);   // no key is sent

// Search, list posters, download the chosen one (0 = best rated; wraps around).
// The key is only sent to the API, never to the image server.
Result fetchPoster(HttpClient& http, const std::string& base, const std::string& key, const std::string& name, int choice = 0);

// What was looked up already, so a start doesn't ask again for every title
class Memo {
  public:
    struct Entry {
        Outcome outcome = Outcome::Error;
        int64_t gameId = 0;
        int64_t time = 0;
        int64_t gridId = 0;
        bool manual = false;        // picked by the user: the automatic lookup leaves it alone
        std::string gameName;       // SteamGridDB's name of the matched game
    };

    // Not asked yet, or nothing was found more than retryDays ago (never for found or hidden titles)
    bool shouldFetch(TitleId id, int64_t now, int retryDays = 30) const;
    // Errors and BadKey are not recorded
    void record(TitleId id, Outcome outcome, int64_t gameId, int64_t now, const std::string& gameName = "", int64_t gridId = 0,
                bool manual = false);
    void forget(TitleId id);
    const Entry* find(TitleId id) const;
    size_t size() const { return entries_.size(); }

    std::string serialize() const;
    static Memo parse(const std::string& text);

  private:
    std::map<TitleId, Entry> entries_;
};

}  // namespace lib::griddb
