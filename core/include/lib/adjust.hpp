#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>

#include "lib/types.hpp"

namespace lib {

// Manual playtime corrections set by the user, per profile and title (seconds, may be negative).
// They are added on top of the recorded playtime when the library is built; nothing else changes.
class AdjustmentStore {
  public:
    int64_t get(TitleId, const Uid&) const;     // 0 if none
    void set(TitleId, const Uid&, int64_t seconds);   // 0 removes the entry
    size_t size() const { return values_.size(); }

    std::string serialize() const;
    static AdjustmentStore parse(const std::string& text);

  private:
    std::map<std::pair<TitleId, Uid>, int64_t> values_;
};

}  // namespace lib
