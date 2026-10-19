#include "analytics/LineageTracker.hpp"

namespace evo {

void LineageTracker::recordBirth(const Organism& o, std::uint64_t tick) {
    if (o.id != records_.size() + 1 || records_.size() >= maxRecords_) {
        truncated_ = true;  // ids must stay dense; stop recording rather than corrupt
        return;
    }
    LineageRecord r;
    r.parentA = o.parentA;
    r.parentB = o.parentB;
    r.founder = o.founder;
    r.birthTick = static_cast<std::uint32_t>(tick);
    r.generation = o.generation;
    r.kind = static_cast<std::uint8_t>(o.kind);
    records_.push_back(r);
    if (o.parentA && has(o.parentA)) ++records_[o.parentA - 1].offspring;
    if (o.parentB && o.parentB != o.parentA && has(o.parentB)) ++records_[o.parentB - 1].offspring;
}

void LineageTracker::recordDeath(std::uint64_t id, std::uint64_t tick, DeathCause cause) {
    if (!has(id)) return;
    records_[id - 1].deathTick = static_cast<std::uint32_t>(tick);
    records_[id - 1].cause = static_cast<std::uint8_t>(cause);
}

std::uint64_t LineageTracker::descendantCount(std::uint64_t id) const {
    if (!has(id)) return 0;
    std::vector<std::uint8_t> mark(records_.size() + 1, 0);
    mark[id] = 1;
    std::uint64_t n = 0;
    for (std::uint64_t k = id + 1; k <= records_.size(); ++k) {
        const auto& r = records_[k - 1];
        if ((r.parentA && mark[r.parentA]) || (r.parentB && mark[r.parentB])) {
            mark[k] = 1;
            ++n;
        }
    }
    return n;
}

std::vector<std::uint64_t> LineageTracker::ancestors(std::uint64_t id, std::size_t maxDepth) const {
    std::vector<std::uint64_t> out;
    const LineageRecord* r = get(id);
    while (r && r->parentA && out.size() < maxDepth) {
        out.push_back(r->parentA);
        r = get(r->parentA);
    }
    return out;
}

std::vector<std::uint64_t> LineageTracker::children(std::uint64_t id, std::size_t limit) const {
    std::vector<std::uint64_t> out;
    for (std::uint64_t k = id + 1; k <= records_.size() && out.size() < limit; ++k) {
        const auto& r = records_[k - 1];
        if (r.parentA == id || r.parentB == id) out.push_back(k);
    }
    return out;
}

}  // namespace evo
