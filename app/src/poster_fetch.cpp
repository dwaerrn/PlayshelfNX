#include "poster_fetch.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <thread>

#include <borealis.hpp>
#include <borealis/core/cache_helper.hpp>

#include "app_data.hpp"
#include "lib/griddb.hpp"
#include "lib/types.hpp"
#include "stb_image.h"   // implementation is in Borealis (nanovg)

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

namespace {

constexpr size_t kMaxDownload = 12 * 1024 * 1024;

std::atomic<bool> g_stop{false};
std::atomic<bool> g_running{false};
std::thread g_worker;

std::string apiBase() {
#ifndef __SWITCH__
    // PC testing against a local fake server
    if (const char* b = std::getenv("LIBRARY_SGDB_BASE")) return b;
#endif
    return lib::griddb::kApiBase;
}

class CurlClient : public lib::griddb::HttpClient {
  public:
    explicit CurlClient(const std::atomic<bool>* stop) : stop_(stop) { }

    lib::griddb::HttpResponse get(const std::string& url, const std::vector<std::string>& headers) override {
        lib::griddb::HttpResponse r;
        CURL* curl = curl_easy_init();
        if (!curl) {
            r.error = "curl_easy_init failed";
            return r;
        }
        curl_slist* list = nullptr;
        for (const std::string& h : headers) list = curl_slist_append(list, h.c_str());
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 40L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Library-Switch/0.1");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &CurlClient::onData);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &r.body);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, &CurlClient::onProgress);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, const_cast<std::atomic<bool>*>(stop_));
        const CURLcode rc = curl_easy_perform(curl);
        if (rc == CURLE_OK) {
            long code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
            r.status = static_cast<int>(code);
        } else {
            r.error = curl_easy_strerror(rc);
            r.body.clear();
        }
        curl_slist_free_all(list);
        curl_easy_cleanup(curl);
        return r;
    }

  private:
    const std::atomic<bool>* stop_;

    static size_t onData(char* data, size_t size, size_t count, void* user) {
        std::string* body = static_cast<std::string*>(user);
        const size_t n = size * count;
        if (body->size() + n > kMaxDownload) return 0;     // too big: abort
        body->append(data, n);
        return n;
    }
    static int onProgress(void* stop, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
        return (g_stop || (stop && *static_cast<std::atomic<bool>*>(stop))) ? 1 : 0;   // closing: abort the transfer
    }
};

void appendJpeg(void* ctx, void* data, int size) {
    static_cast<std::string*>(ctx)->append(static_cast<const char*>(data), static_cast<size_t>(size));
}

// Decodes a downloaded image, scales it to cover outW x outH (centre crop) and encodes a JPEG
bool toJpeg(const std::string& file, int outW, int outH, std::string& out) {
    const int kPosterW = outW, kPosterH = outH;
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(file.data()), static_cast<int>(file.size()), &w, &h, &n, 3);
    if (!px || w <= 0 || h <= 0) {
        if (px) stbi_image_free(px);
        return false;
    }
    // Area of the source that maps onto the poster
    const double scale = std::max(static_cast<double>(kPosterW) / w, static_cast<double>(kPosterH) / h);
    const double cropW = kPosterW / scale, cropH = kPosterH / scale;
    const double x0 = (w - cropW) / 2, y0 = (h - cropH) / 2;
    std::vector<unsigned char> dst(static_cast<size_t>(kPosterW) * kPosterH * 3);
    // Box filter: each output pixel averages the source pixels it covers
    for (int y = 0; y < kPosterH; y++) {
        const int sy0 = std::max(0, static_cast<int>(y0 + y / scale));
        const int sy1 = std::min(h, std::max(sy0 + 1, static_cast<int>(y0 + (y + 1) / scale)));
        for (int x = 0; x < kPosterW; x++) {
            const int sx0 = std::max(0, static_cast<int>(x0 + x / scale));
            const int sx1 = std::min(w, std::max(sx0 + 1, static_cast<int>(x0 + (x + 1) / scale)));
            unsigned sum[3] = {0, 0, 0};
            unsigned count = 0;
            for (int sy = sy0; sy < sy1; sy++) {
                const unsigned char* row = px + (static_cast<size_t>(sy) * w + sx0) * 3;
                for (int sx = sx0; sx < sx1; sx++, row += 3) {
                    sum[0] += row[0];
                    sum[1] += row[1];
                    sum[2] += row[2];
                    count++;
                }
            }
            unsigned char* d = &dst[(static_cast<size_t>(y) * kPosterW + x) * 3];
            for (int c = 0; c < 3; c++) d[c] = static_cast<unsigned char>(count ? sum[c] / count : 0);
        }
    }
    stbi_image_free(px);
    out.clear();
    return stbi_write_jpg_to_func(appendJpeg, &out, kPosterW, kPosterH, 3, dst.data(), 88) != 0 && !out.empty();
}

// Saved poster size: SteamGridDB's native size, enough for a focused card docked (1080p)
constexpr int kPosterW = 600, kPosterH = 900;
constexpr int kThumbW = 300, kThumbH = 450;       // previews on the Change poster screen

std::string memoPath() {
    return appDataDir() + "steamgriddb_lookups.txt";
}

// Both threads change the lookup file: always read, change and write it under this lock
std::mutex& memoMutex() {
    static std::mutex m;
    return m;
}

lib::griddb::Memo loadMemo() {
    std::lock_guard<std::mutex> lock(memoMutex());
    return lib::griddb::Memo::parse(readDataFile(memoPath()));
}

void updateMemo(const std::function<void(lib::griddb::Memo&)>& change) {
    std::lock_guard<std::mutex> lock(memoMutex());
    lib::griddb::Memo memo = lib::griddb::Memo::parse(readDataFile(memoPath()));
    change(memo);
    writeDataFile(memoPath(), memo.serialize());
}

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// Removes grids/<ID>.jpg and grids/<ID>-*.jpg
void removeGridFiles(uint64_t id) {
    const std::string hex = lib::titleHex(id);
    std::error_code ec;
    std::vector<std::filesystem::path> doomed;
    for (const auto& f : std::filesystem::directory_iterator(appDataDir() + "grids", ec)) {
        const std::string stem = upper(f.path().stem().string());
        if (stem == hex || stem.rfind(hex + "-", 0) == 0) doomed.push_back(f.path());
    }
    for (const auto& path : doomed) std::filesystem::remove(path, ec);
}

std::string thumbDir() {
    return appDataDir() + "thumbs/";
}

// Posters saved before the size went up (300x450) are downloaded again at full size, from the
// same SteamGridDB poster (the memo knows game and poster ID; the user's own picks stay theirs).
// Returns false if the run has to stop (bad key, too many errors).
bool upgradeOldPosters(lib::griddb::HttpClient& http, const std::string& key) {
    using namespace lib::griddb;
    std::vector<std::pair<uint64_t, std::string>> old;
    std::error_code ec;
    for (const auto& f : std::filesystem::directory_iterator(appDataDir() + "grids", ec)) {
        const std::string stem = upper(f.path().stem().string());
        lib::TitleId id = 0;
        int w = 0, h = 0, n = 0;
        if (stem.size() < 16 || !lib::parseTitleId(stem.substr(0, 16), id)) continue;
        if (stbi_info(f.path().string().c_str(), &w, &h, &n) && h < kPosterH) old.emplace_back(id, f.path().string());
    }
    if (old.empty()) return true;
    appLog("SteamGridDB: " + std::to_string(old.size()) + " posters to download again at " + std::to_string(kPosterW) + "x" + std::to_string(kPosterH));

    int done = 0, inARow = 0;
    for (const auto& [id, path] : old) {
        if (g_stop) return false;
        const Memo memo = loadMemo();
        const Memo::Entry* e = memo.find(id);
        if (!e || e->outcome != Outcome::Found || e->gameId == 0) continue;
        const Memo::Entry before = *e;

        std::vector<Grid> grids;
        std::string message, file, jpeg;
        Outcome o = listGrids(http, apiBase(), key, before.gameId, grids, message);
        if (o == Outcome::BadKey) return false;
        if (o == Outcome::Error) {
            appLog("SteamGridDB: " + lib::titleHex(id) + ": " + message);
            if (++inARow >= 3) return false;
            continue;
        }
        inARow = 0;
        const Grid* pick = nullptr;
        for (const Grid& g : grids) {
            if (g.id == before.gridId) pick = &g;
        }
        if (!pick && before.gridId == 0 && !before.manual && !grids.empty()) pick = &grids.front();
        if (!pick) continue;     // that poster is gone from SteamGridDB: keep the old file
        if (download(http, pick->url, file, message) != Outcome::Found || !toJpeg(file, kPosterW, kPosterH, jpeg)) continue;

        // The user may have picked another poster meanwhile
        const Memo latest = loadMemo();
        const Memo::Entry* now = latest.find(id);
        if (!now || now->gridId != before.gridId || now->manual != before.manual || !std::filesystem::exists(path, ec)) continue;
        if (!writeDataFile(path, jpeg)) continue;
        done++;
        // Same file name: make the texture cache read it again next time a card needs it
        brls::sync([path] {
            const int tex = brls::TextureCache::instance().getCache(path);
            if (tex > 0) {
                brls::TextureCache::instance().markDirty(tex);
                brls::TextureCache::instance().removeCache(tex);
            }
        });
        for (int i = 0; i < 5 && !g_stop; i++) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    appLog("SteamGridDB: " + std::to_string(done) + " posters downloaded again at full size");
    return true;
}

void run(std::vector<PosterJob> jobs, std::string key, std::function<void(uint64_t, const std::string&)> onReady) {
    using namespace lib::griddb;
    CurlClient http(nullptr);
    int found = 0, missing = 0, errors = 0, inARow = 0;
    bool badKey = false;
    const auto started = std::chrono::steady_clock::now();

    for (const PosterJob& job : jobs) {
        if (g_stop) break;
        Result r = fetchPoster(http, apiBase(), key, job.name);
        const int64_t now = static_cast<int64_t>(std::time(nullptr));
        if (r.outcome == Outcome::BadKey) {
            appLog("SteamGridDB: the API key was refused (check steamgriddb.txt)");
            brls::sync([] { brls::Application::notify("SteamGridDB refused the API key"); });
            badKey = true;
            break;
        }
        if (r.outcome == Outcome::Error) {
            errors++;
            appLog("SteamGridDB: " + lib::titleHex(job.id) + ": " + r.message);
            if (++inARow >= 3) {
                appLog("SteamGridDB: stopped after 3 errors in a row");
                break;
            }
            continue;
        }
        inARow = 0;
        // The user may have picked a poster on the Change poster screen meanwhile
        const Memo latest = loadMemo();
        if (const Memo::Entry* e = latest.find(job.id)) {
            if (e->manual || e->outcome == Outcome::Hidden) continue;
        }
        if (r.outcome == Outcome::Found) {
            std::string jpeg;
            const std::string path = gridPosterPathFor(job.id);
            if (toJpeg(r.image, kPosterW, kPosterH, jpeg) && writeDataFile(path, jpeg)) {
                found++;
                updateMemo([&](Memo& m) { m.record(job.id, Outcome::Found, r.gameId, now, r.gameName, r.gridId); });
                brls::sync([onReady, id = job.id, path] { onReady(id, path); });
            } else {
                errors++;
                appLog("SteamGridDB: " + lib::titleHex(job.id) + ": could not decode or save the poster");
                continue;
            }
        } else {
            missing++;
            updateMemo([&](Memo& m) { m.record(job.id, r.outcome, r.gameId, now, r.gameName); });
        }
        // Be gentle with the free API
        for (int i = 0; i < 5 && !g_stop; i++) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!g_stop && !badKey && inARow < 3) upgradeOldPosters(http, key);
    g_running = false;
    const long secs = static_cast<long>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started).count());
    appLog("SteamGridDB: " + std::to_string(found) + " posters, " + std::to_string(missing) + " not found, " + std::to_string(errors) +
           " errors (" + std::to_string(jobs.size()) + " asked, " + std::to_string(secs) + " s)");
}

}  // namespace

std::string steamGridKey() {
    std::string text = readDataFile(appDataDir() + "steamgriddb.txt");
    // First line; spaces and a "Bearer " prefix pasted by mistake are removed
    text = text.substr(0, text.find_first_of("\r\n"));
    text.erase(std::remove_if(text.begin(), text.end(), [](char c) { return c == ' ' || c == '\t'; }), text.end());
    if (text.rfind("Bearer", 0) == 0) text = text.substr(6);
    return text;
}

void startPosterFetch(std::vector<PosterJob> jobs, std::function<void(uint64_t, const std::string&)> onReady) {
    if (g_running) return;
    if (g_worker.joinable()) g_worker.join();     // the previous run is over
    const std::string key = steamGridKey();
    if (key.empty()) return;
    // Titles that were looked up before are skipped (misses are retried after 30 days)
    lib::griddb::Memo memo = loadMemo();
    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    jobs.erase(std::remove_if(jobs.begin(), jobs.end(), [&](const PosterJob& j) { return !memo.shouldFetch(j.id, now); }), jobs.end());
    // Runs even without new titles: old small posters may need downloading again
    static std::once_flag curlInit;
    std::call_once(curlInit, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    appLog("SteamGridDB: looking up " + std::to_string(jobs.size()) + " titles");
    g_stop = false;
    g_running = true;
    g_worker = std::thread(run, std::move(jobs), key, std::move(onReady));
}

void stopPosterFetch() {
    g_stop = true;
    if (g_worker.joinable()) g_worker.join();
}

PosterMemo posterMemo(uint64_t titleId) {
    PosterMemo out;
    const lib::griddb::Memo memo = loadMemo();
    if (const lib::griddb::Memo::Entry* e = memo.find(titleId)) {
        out.known = true;
        out.hidden = e->outcome == lib::griddb::Outcome::Hidden;
        out.gameId = e->gameId;
        out.gameName = e->gameName;
        out.gridId = e->gridId;
    }
    return out;
}

void hideDownloadedPoster(uint64_t titleId) {
    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    updateMemo([&](lib::griddb::Memo& m) {
        const lib::griddb::Memo::Entry* e = m.find(titleId);
        const int64_t gameId = e ? e->gameId : 0;
        const std::string name = e ? e->gameName : "";
        m.record(titleId, lib::griddb::Outcome::Hidden, gameId, now, name, 0, true);
    });
    removeGridFiles(titleId);
    appLog("SteamGridDB: " + lib::titleHex(titleId) + ": poster removed by the user");
}

// ---- PosterBrowser ----

PosterBrowser::PosterBrowser() : key_(steamGridKey()) {
    static std::once_flag curlInit;
    std::call_once(curlInit, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    worker_ = std::thread([this] { loop(); });
}

PosterBrowser::~PosterBrowser() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
        tasks_.clear();
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
    // Previews are only for this screen
    std::error_code ec;
    std::filesystem::remove_all(thumbDir(), ec);
}

void PosterBrowser::post(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push_back(std::move(task));
    }
    wake_.notify_one();
}

void PosterBrowser::loop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
            if (stop_) return;
            task = std::move(tasks_.front());
            tasks_.pop_front();
        }
        task();
    }
}

namespace {
// Runs fn on the UI thread unless the browser is gone by then
void deliver(const std::weak_ptr<int>& alive, std::function<void()> fn) {
    brls::sync([alive, fn] {
        if (alive.lock()) fn();
    });
}
}  // namespace

void PosterBrowser::search(const std::string& term, std::function<void(bool, std::vector<GameInfo>, std::string)> done) {
    std::weak_ptr<int> alive = alive_;
    post([this, term, done, alive] {
        CurlClient http(&stop_);
        std::vector<lib::griddb::Game> games;
        std::string message;
        const lib::griddb::Outcome o = lib::griddb::searchGames(http, apiBase(), key_, term, games, message);
        std::vector<GameInfo> out;
        for (const auto& g : games) out.push_back({g.id, g.name});
        const bool ok = o == lib::griddb::Outcome::Found || o == lib::griddb::Outcome::NoMatch;
        if (!ok) appLog("SteamGridDB browse: " + message);
        deliver(alive, [done, ok, out, message] { done(ok, out, message); });
    });
}

void PosterBrowser::listPosters(int64_t gameId, std::function<void(bool, std::vector<Choice>, std::string)> done) {
    std::weak_ptr<int> alive = alive_;
    post([this, gameId, done, alive] {
        CurlClient http(&stop_);
        std::vector<lib::griddb::Grid> grids;
        std::string message;
        const lib::griddb::Outcome o = lib::griddb::listGrids(http, apiBase(), key_, gameId, grids, message);
        std::vector<Choice> out;
        for (const auto& g : grids) out.push_back({g.id, g.url, g.thumb});
        const bool ok = o == lib::griddb::Outcome::Found || o == lib::griddb::Outcome::NoGrid;
        if (!ok) appLog("SteamGridDB browse: " + message);
        deliver(alive, [done, ok, out, message] { done(ok, out, message); });
    });
}

void PosterBrowser::preview(const Choice& choice, std::function<void(std::string)> done) {
    std::weak_ptr<int> alive = alive_;
    const int generation = previewGeneration_;
    post([this, choice, done, alive, generation] {
        if (generation != previewGeneration_) return;   // another game was chosen meanwhile
        const std::string path = thumbDir() + std::to_string(choice.id) + ".jpg";
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            CurlClient http(&stop_);
            std::string file, jpeg, message;
            // The small preview first; the full poster if there is none or it can't be decoded (e.g. WebP)
            bool ok = !choice.thumb.empty() && lib::griddb::download(http, choice.thumb, file, message) == lib::griddb::Outcome::Found &&
                      toJpeg(file, kThumbW, kThumbH, jpeg);
            if (!ok && !stop_) {
                ok = lib::griddb::download(http, choice.url, file, message) == lib::griddb::Outcome::Found && toJpeg(file, kThumbW, kThumbH, jpeg);
            }
            if (!ok || !writeDataFile(path, jpeg)) {
                deliver(alive, [done] { done(""); });
                return;
            }
        }
        deliver(alive, [done, path] { done(path); });
    });
}

void PosterBrowser::cancelPreviews() {
    previewGeneration_++;
}

void PosterBrowser::choose(uint64_t titleId, int64_t gameId, const std::string& gameName, const Choice& choice,
                           std::function<void(bool, std::string)> done) {
    std::weak_ptr<int> alive = alive_;
    post([this, titleId, gameId, gameName, choice, done, alive] {
        CurlClient http(&stop_);
        std::string file, jpeg, message;
        bool ok = lib::griddb::download(http, choice.url, file, message) == lib::griddb::Outcome::Found;
        if (ok && !toJpeg(file, kPosterW, kPosterH, jpeg)) {
            ok = false;
            message = "the poster could not be decoded";
        }
        if (ok) {
            // A new file name, so the texture cache can't show the old picture
            removeGridFiles(titleId);
            const std::string path = appDataDir() + "grids/" + lib::titleHex(titleId) + "-" + std::to_string(choice.id) + ".jpg";
            ok = writeDataFile(path, jpeg);
            if (ok) {
                const int64_t now = static_cast<int64_t>(std::time(nullptr));
                updateMemo([&](lib::griddb::Memo& m) { m.record(titleId, lib::griddb::Outcome::Found, gameId, now, gameName, choice.id, true); });
                appLog("SteamGridDB: " + lib::titleHex(titleId) + ": poster " + std::to_string(choice.id) + " chosen by the user");
            } else {
                message = "the poster could not be saved";
            }
        }
        if (!ok) appLog("SteamGridDB browse: " + message);
        deliver(alive, [done, ok, message] { done(ok, message); });
    });
}
