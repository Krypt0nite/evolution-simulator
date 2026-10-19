#pragma once
// Genome-distance clustering ("genetic clusters", not proven species).
// Leader clustering in id order (deterministic), then centroids are matched to
// the previous pass so cluster ids – and therefore colours – stay stable.
#include <cstdint>
#include <vector>

#include "organisms/Organism.hpp"

namespace evo {

struct GeneticCluster {
    int id = 0;
    std::vector<double> centroid;  // named traits only
    int size = 0;
    int preySize = 0;
    int predatorSize = 0;
};

class Speciation {
public:
    void configure(double threshold, int maxClusters = 48) { threshold_ = threshold; maxClusters_ = maxClusters; clusters_.clear(); nextId_ = 0; }
    // Assigns organisms[i].cluster.
    void update(std::vector<Organism>& organisms);

    const std::vector<GeneticCluster>& clusters() const { return clusters_; }
    std::vector<GeneticCluster>& mutableClusters() { return clusters_; }
    int nextId() const { return nextId_; }
    void setNextId(int n) { nextId_ = n; }

private:
    double threshold_ = 0.2;
    int maxClusters_ = 48;
    int nextId_ = 0;
    std::vector<GeneticCluster> clusters_;
};

double traitDistance(const std::vector<double>& a, const Genome& g);

}  // namespace evo
