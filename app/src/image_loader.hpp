#pragma once

// Decodes poster and icon files on a worker thread, so scrolling never waits for a JPEG.
// The texture is created on the main thread and goes into Borealis' TextureCache under the
// file path, so a later Image::setImageFromFile(path) is a cheap cache hit.

#include <borealis.hpp>
#include <memory>
#include <string>

namespace imageload {

// Shows `path` in `image`. A cached texture is set right away (returns true). Otherwise the file is
// decoded in the background (scaled down to cover maxW x maxH if it is larger) and set later, if
// `alive` still exists then; onShown runs after that (for a fade-in). Returns false in that case.
bool show(brls::Image* image, const std::string& path, int maxW, int maxH, std::weak_ptr<void> alive,
          std::function<void()> onShown = nullptr);

// Ends the worker thread (at app exit)
void stop();

}  // namespace imageload
