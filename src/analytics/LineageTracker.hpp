#pragma once
// Birth/death record for every organism ever born, indexed densely by id
// (ids start at 1 and increase), so parent lookups are O(1) and descendant
// counting is a single forward pass (children always have larger ids).
#include <cstdint>
#include <vector>

#include "organisms/Organism.hpp"

namespace evo {

struct LineageRecord {
    std::uint64_t parentA = 0, parentB = 0, founder = 0;
    std::uint32_t birthTick = 0;
    std::uint32_t deathTick = UINT32_MAX;  // UINT32_MAX = still alive
    std::uint32_t generation = 0;
    std::uint32_t offspring = 0;
    std::uint8_t kind = 0;
    std::uint8_t cause = 0;
};

class LineageTracker {
public:
    void configure(std::size_t maxRecords) { maxRecords_ = maxRecords; records_.clear(); truncated_ = false; }

    void recordBirth(const Organism& o, std::uint64_t tick);
    void recordDeath(std::uint64_t id, std::uint64_t tick, DeathCause cause);

    bool has(std::uint64_t id) const { return id >= 1 && id <= records_.size(); }
    const LineageRecord* get(std::uint64_t id) const { return has(id) ? &records_[id - 1] : nullptr; }
    std::size_t size() const { return records_.size(); }
    bool truncated() const { return truncated_; }

    // Total number of descendants (all generations) of `id`.
    std::uint64_t descendantCount(std::uint64_t id) const;
    // Ancestor chain following parentA (closest first), up to maxDepth.
    std::vector<std::uint64_t> ancestors(std::uint64_t id, std::size_t maxDepth) const;
    // Direct children ids (scan; O(n)).
    std::vector<std::uint64_t> children(std::uint64_t id, std::size_t limit) const;

    const std::vector<LineageRecord>& records() const { return records_; }
    std::vector<LineageRecord>& mutableRecords() { return records_; }
    void setTruncated(bool t) { truncated_ = t; }

private:
    std::vector<LineageRecord> records_;
    std::size_t maxRecords_ = 3000000;
    bool truncated_ = false;
};

}  // namespace evo
