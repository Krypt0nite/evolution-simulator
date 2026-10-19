#include "analytics/Speciation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace evo {

double traitDistance(const std::vector<double>& c, const Genome& g) {
    double s = 0.0;
    for (int i = 0; i < kNumTraits; ++i) {
        const auto& spec = geneSpec(i);
        const double d = (c[static_cast<std::size_t>(i)] - g[i]) / (spec.max - spec.min);
        s += d * d;
    }
    return std::sqrt(s / static_cast<double>(kNumTraits));
}

static double centroidDistance(const std::vector<double>& a, const std::vector<double>& b) {
    double s = 0.0;
    for (int i = 0; i < kNumTraits; ++i) {
        const auto& spec = geneSpec(i);
        const double d = (a[static_cast<std::size_t>(i)] - b[static_cast<std::size_t>(i)]) / (spec.max - spec.min);
        s += d * d;
    }
    return std::sqrt(s / static_cast<double>(kNumTraits));
}

void Speciation::update(std::vector<Organism>& organisms) {
    // 1. Leader clustering. Prey and predators are clustered separately.
    struct Work { std::vector<double> leader; std::vector<double> sum; int n = 0; int prey = 0; int pred = 0; Kind kind; };
    std::vector<Work> work;
    std::vector<int> assign(organisms.size(), -1);
    for (std::size_t i = 0; i < organisms.size(); ++i) {
        const auto& o = organisms[i];
        if (!o.alive) continue;
        int best = -1;
        double bestD = std::numeric_limits<double>::max();
        for (std::size_t k = 0; k < work.size(); ++k) {
            if (work[k].kind != o.kind) continue;
            const double d = traitDistance(work[k].leader, o.genome);
            if (d < bestD) { bestD = d; best = static_cast<int>(k); }
        }
        if (best < 0 || (bestD > threshold_ && static_cast<int>(work.size()) < maxClusters_)) {
            Work w;
            w.leader.assign(o.genome.genes.begin(), o.genome.genes.begin() + kNumTraits);
            w.sum.assign(kNumTraits, 0.0);
            w.kind = o.kind;
            work.push_back(std::move(w));
            best = static_cast<int>(work.size()) - 1;
        }
        auto& w = work[static_cast<std::size_t>(best)];
        for (int g = 0; g < kNumTraits; ++g) w.sum[static_cast<std::size_t>(g)] += o.genome[g];
        ++w.n;
        (o.kind == Kind::Prey ? w.prey : w.pred)++;
        assign[i] = best;
    }

    // 2. Match new clusters to previous ones (largest first) to keep ids stable.
    std::vector<std::size_t> order(work.size());
    for (std::size_t k = 0; k < order.size(); ++k) order[k] = k;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return work[a].n > work[b].n; });
    std::vector<bool> used(clusters_.size(), false);
    std::vector<GeneticCluster> next(work.size());
    for (std::size_t k : order) {
        auto& w = work[k];
        GeneticCluster c;
        c.centroid.resize(kNumTraits);
        for (int g = 0; g < kNumTraits; ++g) c.centroid[static_cast<std::size_t>(g)] = w.sum[static_cast<std::size_t>(g)] / w.n;
        c.size = w.n;
        c.preySize = w.prey;
        c.predatorSize = w.pred;
        int match = -1;
        double bestD = threshold_;
        for (std::size_t p = 0; p < clusters_.size(); ++p) {
            if (used[p]) continue;
            const bool prevPred = clusters_[p].predatorSize > clusters_[p].preySize;
            if (prevPred != (w.kind == Kind::Predator)) continue;
            const double d = centroidDistance(clusters_[p].centroid, c.centroid);
            if (d <= bestD) { bestD = d; match = static_cast<int>(p); }
        }
        if (match >= 0) {
            used[static_cast<std::size_t>(match)] = true;
            c.id = clusters_[static_cast<std::size_t>(match)].id;
        } else {
            c.id = nextId_++;
        }
        next[k] = std::move(c);
    }
    for (std::size_t i = 0; i < organisms.size(); ++i)
        organisms[i].cluster = assign[i] >= 0 ? next[static_cast<std::size_t>(assign[i])].id : -1;
    std::stable_sort(next.begin(), next.end(), [](const GeneticCluster& a, const GeneticCluster& b) { return a.size > b.size; });
    clusters_ = std::move(next);
}

}  // namespace evo
