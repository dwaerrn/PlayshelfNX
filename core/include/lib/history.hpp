#pragma once

#include <string>
#include <utility>
#include <vector>

#include "lib/types.hpp"

namespace lib {

// The app's own copy of the play event log. The system's log is limited and forgets old
// events (and the statistics of deleted titles go away), so every start merges the system's
// events into this store, which only ever grows.
class HistoryStore {
  public:
    const std::vector<RawEvent>& events() const { return events_; }

    // Adds the events that aren't stored yet. Events that already exist are matched one to one,
    // so legitimately repeated entries are kept. Returns the number of events added.
    size_t merge(const std::vector<RawEvent>& fresh);

    std::string serialize() const;
    static HistoryStore parse(const std::string& text);

  private:
    std::vector<RawEvent> events_;
};

// Names of every title the app has ever seen installed, so deleted titles keep their name
class NameCache {
  public:
    void update(const std::vector<TitleInfo>& titles);   // only stores non-empty names
    std::string name(TitleId id) const;                   // empty if unknown

    std::string serialize() const;
    static NameCache parse(const std::string& text);

  private:
    std::vector<std::pair<TitleId, std::string>> names_;
};

}  // namespace lib
