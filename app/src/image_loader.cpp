#include "image_loader.hpp"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>
#include <vector>

#include <borealis/core/cache_helper.hpp>

#ifdef __SWITCH__
#include <switch.h>
#endif

#include "stb_image.h"   // implementation is in Borealis (nanovg)

namespace imageload {
namespace {

struct Job {
    std::string path;
    int maxW, maxH;
    std::weak_ptr<void> alive;
    std::function<void(int texture)> done;   // main thread; texture holds one cache reference
};

std::mutex mutex;
std::condition_variable wake;
std::deque<Job> queue;
std::thread worker;
bool stopping = false;

// RGBA pixels, box-filtered down so the image just covers maxW x maxH (aspect kept)
bool decode(const std::string& path, int maxW, int maxH, std::vector<unsigned char>& out, int& w, int& h) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::string file((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    int n = 0;
    unsigned char* px = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(file.data()), static_cast<int>(file.size()), &w, &h, &n, 4);
    if (!px || w <= 0 || h <= 0) {
        if (px) stbi_image_free(px);
        return false;
    }
    const double scale = std::max(static_cast<double>(maxW) / w, static_cast<double>(maxH) / h);
    if (scale >= 0.9) {
        out.assign(px, px + static_cast<size_t>(w) * h * 4);
        stbi_image_free(px);
        return true;
    }
    const int ow = std::max(1, static_cast<int>(w * scale + 0.5)), oh = std::max(1, static_cast<int>(h * scale + 0.5));
    out.assign(static_cast<size_t>(ow) * oh * 4, 0);
    for (int y = 0; y < oh; y++) {
        const int sy0 = std::min(h - 1, static_cast<int>(y / scale));
        const int sy1 = std::min(h, std::max(sy0 + 1, static_cast<int>((y + 1) / scale)));
        for (int x = 0; x < ow; x++) {
            const int sx0 = std::min(w - 1, static_cast<int>(x / scale));
            const int sx1 = std::min(w, std::max(sx0 + 1, static_cast<int>((x + 1) / scale)));
            unsigned sum[4] = {0, 0, 0, 0}, count = 0;
            for (int sy = sy0; sy < sy1; sy++) {
                const unsigned char* p = px + (static_cast<size_t>(sy) * w + sx0) * 4;
                for (int sx = sx0; sx < sx1; sx++, p += 4, count++) {
                    for (int c = 0; c < 4; c++) sum[c] += p[c];
                }
            }
            unsigned char* d = &out[(static_cast<size_t>(y) * ow + x) * 4];
            for (int c = 0; c < 4; c++) d[c] = static_cast<unsigned char>(sum[c] / count);
        }
    }
    stbi_image_free(px);
    w = ow;
    h = oh;
    return true;
}

void run() {
#ifdef __SWITCH__
    // libnx starts threads on the main thread's core; decode on core 1 so the UI thread keeps core 0,
    // and at a slightly lower priority than the UI
    svcSetThreadCoreMask(CUR_THREAD_HANDLE, 1, 1 << 1);
    svcSetThreadPriority(CUR_THREAD_HANDLE, 0x2D);
#endif
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait(lock, [] { return stopping || !queue.empty(); });
            if (stopping) return;
            job = std::move(queue.front());
            queue.pop_front();
        }
        if (job.alive.expired()) continue;      // the card scrolled away before its turn

        auto pixels = std::make_shared<std::vector<unsigned char>>();
        int w = 0, h = 0;
        if (!decode(job.path, job.maxW, job.maxH, *pixels, w, h)) continue;

        brls::sync([job, pixels, w, h]() {
            // Another request may have loaded the same file meanwhile
            int tex = brls::TextureCache::instance().getCache(job.path);
            if (tex <= 0) {
                tex = nvgCreateImageRGBA(brls::Application::getNVGContext(), w, h, 0, pixels->data());
                if (tex <= 0) return;
                brls::TextureCache::instance().addCache(job.path, tex);    // one reference: ours
            }
            job.done(tex);
        });
    }
}

}  // namespace

bool show(brls::Image* image, const std::string& path, int maxW, int maxH, std::weak_ptr<void> alive, std::function<void()> onShown) {
    if (brls::TextureCache::instance().getCache(path) > 0) {
        image->setImageFromFile(path);                      // cache hit: one more reference
        brls::TextureCache::instance().removeCache(image->getTexture());   // drop getCache()'s
        return true;
    }

    // Docked the Switch renders at 1920x1080 (1.5x the UI's 1280x720): decode for that, so a poster is
    // never stretched on a TV. Fixed, not the current scale, so docking later needs no reload.
    constexpr float kRenderScale = 1.5f;
    Job job;
    job.path = path;
    job.maxW = static_cast<int>(maxW * kRenderScale + 0.5f);
    job.maxH = static_cast<int>(maxH * kRenderScale + 0.5f);
    job.alive = alive;
    job.done = [image, path, alive, onShown](int tex) {
        if (!alive.expired()) {
            image->setImageFromFile(path);                  // cache hit now
            if (onShown) onShown();
        }
        brls::TextureCache::instance().removeCache(tex);    // the loader's reference
    };
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!worker.joinable() && !stopping) worker = std::thread(run);
        queue.push_back(std::move(job));
    }
    wake.notify_one();
    return false;
}

void stop() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
        queue.clear();
    }
    wake.notify_one();
    if (worker.joinable()) worker.join();
}

}  // namespace imageload
