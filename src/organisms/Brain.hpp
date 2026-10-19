#pragma once
// Behavioural decision system. A Controller maps (organism state, perception)
// to an Intent. Perception is built by the simulation from information inside
// the organism's vision radius only; controllers never see the world directly.
//
// Two controllers ship:
//   * UtilityController – scores a fixed set of actions using heritable weights
//     (wForage, wExplore, wRest, wFlee, wMate, social, riskTolerance).
//   * NeuralController  – a small evolved MLP that outputs a steering vector
//     and speed directly from the same perception inputs.
// Adding a controller = implement decide() + genomeExtraSize().
#include <memory>
#include <string>

#include "core/Random.hpp"
#include "core/Vec2.hpp"
#include "organisms/Organism.hpp"

namespace evo {

struct Perception {
    double hunger = 0.0;  // 1 - energy / maxEnergy
    double vision = 1.0;

    bool foodSeen = false;  // best food cell (prey) within vision
    Vec2 foodDir;
    double foodDist = 0.0;
    double foodAmount = 0.0;  // biomass / capacity at that cell, [0,1]

    bool threatSeen = false;  // nearest predator within vision (prey only)
    Vec2 threatDir;
    double threatDist = 0.0;

    bool preySeen = false;  // nearest prey within vision (predators only)
    Vec2 preyDir;
    double preyDist = 0.0;

    bool mateSeen = false;  // nearest compatible ready mate (sexual mode)
    Vec2 mateDir;
    double mateDist = 0.0;

    bool hazardSeen = false;  // nearest hazard edge within vision
    Vec2 hazardAway;          // direction away from hazard centre
    double hazardProximity = 0.0;  // 1 inside, → 0 at vision edge

    int neighbours = 0;  // same-kind organisms within vision (attention-capped)
    Vec2 neighbourDir;   // direction to their centroid

    double tempMismatch = 0.0;  // T(here) - thermal optimum
    bool readyToMate = false;
    double ageFrac = 0.0;  // age / lifespan
};

struct Intent {
    Action action = Action::Rest;
    Vec2 dir;               // unit vector (or zero)
    double speedFrac = 0.0; // fraction of max speed, [0,1]
};

class Controller {
public:
    virtual ~Controller() = default;
    virtual Intent decide(const Organism& o, const Perception& p, Rng& rng) const = 0;
    virtual std::size_t genomeExtraSize() const = 0;  // neural weights appended to genome
    virtual const char* name() const = 0;
};

class UtilityController final : public Controller {
public:
    explicit UtilityController(bool sexual) : sexual_(sexual) {}
    Intent decide(const Organism& o, const Perception& p, Rng& rng) const override;
    std::size_t genomeExtraSize() const override { return 0; }
    const char* name() const override { return "utility"; }

private:
    bool sexual_;
};

class NeuralController final : public Controller {
public:
    static constexpr int kInputs = 13;  // includes bias
    static constexpr int kOutputs = 3;  // dx, dy, speed
    explicit NeuralController(int hidden) : hidden_(hidden) {}
    Intent decide(const Organism& o, const Perception& p, Rng& rng) const override;
    std::size_t genomeExtraSize() const override {
        return static_cast<std::size_t>(kInputs * hidden_ + (hidden_ + 1) * kOutputs);
    }
    const char* name() const override { return "neural"; }

private:
    int hidden_;
};

std::unique_ptr<Controller> makeController(const std::string& type, int neuralHidden, bool sexual);

// Small random turn without trigonometry (keeps cross-platform results stable).
Vec2 jitterDirection(Vec2 heading, double sd, Rng& rng);

}  // namespace evo
