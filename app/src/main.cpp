#include <borealis.hpp>
#include <borealis/core/cache_helper.hpp>
#include <cstdlib>
#include <cstring>
#include <string>

#include "library_activity.hpp"
#include "poster_fetch.hpp"
#include "image_loader.hpp"
#include "ui_style.hpp"

int main(int argc, char* argv[])
{
    brls::Logger::setLogLevel(brls::LogLevel::LOG_INFO);
    brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_AUTO;

    ui::setFontPath();
    if (!brls::Application::init()) {
        brls::Logger::error("Unable to init Borealis application");
        return EXIT_FAILURE;
    }

    brls::Application::createWindow("Library");
    ui::loadFonts();
    ui::applyTheme();

    // Keep only a small pool of unused textures around. Borealis would otherwise keep up to
    // 200 posters cached, which is far more than the few cards that are on screen.
    brls::LRUCache<std::string, size_t>::DEFAULT_CAPACITY = 0;
    brls::TextureCache::instance().cache.setCapacity(36);   // live cards (~15) + recently seen ones; ~1.3 MB each at 600x900-class posters
    brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);

    LibraryActivity* library = new LibraryActivity();
    brls::Application::pushActivity(library);
    library->showNewTitlesPrompt();
    library->startPosterDownloads();

    while (brls::Application::mainLoop())
        ;

    stopPosterFetch();
    imageload::stop();

    return EXIT_SUCCESS;
}
