#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// SteamGridDB posters. Needs the user's API key in /config/Library/steamgriddb.txt (PC:
// data/steamgriddb.txt). Posters are stored as 300x450 JPEGs in grids/; the user's own art/
// files always win.
struct PosterJob {
    uint64_t id = 0;
    std::string name;
};

// Automatic lookup in the background, one title at a time. onReady runs on the UI thread for
// every poster that arrives.
void startPosterFetch(std::vector<PosterJob> jobs, std::function<void(uint64_t id, const std::string& path)> onReady);

// Stops the worker (call before the app exits)
void stopPosterFetch();

// The key from steamgriddb.txt, empty if there is none
std::string steamGridKey();

// What the lookup file says about a title
struct PosterMemo {
    bool known = false;
    bool hidden = false;        // the user chose "no poster"
    int64_t gameId = 0;
    std::string gameName;
    int64_t gridId = 0;
};
PosterMemo posterMemo(uint64_t titleId);

// "Use system icon": deletes the downloaded poster and stops the automatic lookup for the title
void hideDownloadedPoster(uint64_t titleId);

// Browsing for the "Change poster" screen. Work runs on a worker thread, one request at a time;
// every callback runs on the UI thread, and never after the browser is destroyed.
class PosterBrowser {
  public:
    struct GameInfo {
        int64_t id = 0;
        std::string name;
    };
    struct Choice {
        int64_t id = 0;
        std::string url;
        std::string thumb;
    };

    PosterBrowser();
    ~PosterBrowser();

    void search(const std::string& term, std::function<void(bool ok, std::vector<GameInfo> games, std::string message)> done);
    void listPosters(int64_t gameId, std::function<void(bool ok, std::vector<Choice> posters, std::string message)> done);
    // 150x225 preview file; an empty path if it could not be loaded. cancelPreviews() drops the queued ones.
    void preview(const Choice& choice, std::function<void(std::string path)> done);
    void cancelPreviews();
    // Downloads the full poster, stores it for the title and remembers the choice
    void choose(uint64_t titleId, int64_t gameId, const std::string& gameName, const Choice& choice,
                std::function<void(bool ok, std::string message)> done);

  private:
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::function<void()>> tasks_;
    std::atomic<bool> stop_{false};
    std::atomic<int> previewGeneration_{0};
    std::shared_ptr<int> alive_ = std::make_shared<int>(0);
    std::string key_;

    void post(std::function<void()> task);
    void loop();
};
