#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "lib/types.hpp"

namespace lib {

// Titles that never get linked to a profile by the system (homebrew forwarders) are "claimed"
// by a profile. Claimed titles with the same name are one game for that profile: the oldest
// claim is the one that is shown, the others are aliases of it. The store also keeps a snapshot
// of each claimed title's statistics so the history survives the title being deleted.
struct ClaimEntry {
    TitleId id = 0;
    std::string name;
    std::vector<Uid> users;      // profiles this title is claimed for
    bool hasSnapshot = false;
    SystemStats snapshot;
};

class ClaimStore {
  public:
    const std::vector<ClaimEntry>& entries() const { return entries_; }

    bool isClaimed(TitleId, const Uid&) const;
    bool isClaimedByAnyone(TitleId) const;

    // Merged into an older claim with the same name (for this profile)?
    bool isAlias(TitleId, const Uid&) const;

    // Every title claimed for the profile, including aliases
    std::vector<TitleId> visible(const Uid&) const;

    // For a claimed, non-merged title: its ID plus everything merged into it, oldest first.
    // Empty if the title isn't claimed for the profile or is itself merged.
    std::vector<TitleId> group(TitleId, const Uid&) const;

    void add(TitleId, const std::string& name, const Uid&);
    void remove(TitleId, const Uid&);   // the entry (and its snapshot) goes when nobody claims it anymore

    // "Don't suggest again" list, per profile
    bool isIgnored(TitleId, const Uid&) const;
    void ignore(TitleId, const Uid&);

    // Remembers the name and merges live statistics into the snapshot (never lowers anything).
    // Returns true if something changed.
    bool updateSnapshot(TitleId, const std::string& name, const std::optional<SystemStats>& live);

    std::string serialize() const;
    static ClaimStore parse(const std::string& text);

  private:
    std::vector<ClaimEntry> entries_;
    std::vector<std::pair<TitleId, Uid>> ignored_;

    const ClaimEntry* find(TitleId) const;
    ClaimEntry* find(TitleId);
    std::vector<const ClaimEntry*> visibleEntries(const Uid&) const;
};

// Name used to decide whether two titles are "the same game" (case/space-insensitive)
std::string normalizeName(const std::string&);

}  // namespace lib
