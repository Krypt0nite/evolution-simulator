#pragma once
#include <array>
#include <cstdint>

#include "core/Vec2.hpp"
#include "organisms/Genome.hpp"
#include "organisms/Phenotype.hpp"

namespace evo {

enum class Action : std::uint8_t { Rest = 0, Forage, Explore, Flee, Hunt, SeekMate, Social, AvoidHazard, Steer, kCount };
const char* actionName(Action a);

enum class DeathCause : std::uint8_t { None = 0, Starvation, OldAge, Predation, Culled };
const char* deathCauseName(DeathCause c);

struct Organism {
    std::uint64_t id = 0;
    std::uint64_t parentA = 0;  // 0 = founder
    std::uint64_t parentB = 0;  // 0 = asexual / founder
    std::uint64_t founder = 0;  // lineage root (follows parentA)
    std::uint32_t generation = 0;
    Kind kind = Kind::Prey;
    bool alive = true;
    DeathCause cause = DeathCause::None;

    Genome genome;
    Phenotype pheno;

    Vec2 pos;
    Vec2 heading{1.0, 0.0};  // unit vector of last intended direction
    double energy = 0.0;
    std::uint32_t age = 0;
    std::uint32_t cooldown = 0;  // ticks until it may reproduce again
    std::uint32_t handling = 0;  // predators: ticks left handling the last kill
    std::uint32_t offspring = 0;
    std::int32_t cluster = -1;   // speciation cluster id (analytics only)

    // Per-tick scratch (written in decide/move, consumed later in the tick).
    Action action = Action::Rest;
    double lastMoved = 0.0;

    // Ring buffer of recent actions for the inspector.
    std::array<Action, 8> recent{};
    std::uint8_t recentHead = 0;

    void pushRecent(Action a) {
        recent[recentHead] = a;
        recentHead = static_cast<std::uint8_t>((recentHead + 1) % recent.size());
    }

    bool mature() const { return age >= pheno.maturity; }
};

}  // namespace evo
