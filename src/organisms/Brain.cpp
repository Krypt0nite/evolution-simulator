#include "organisms/Brain.hpp"

#include "core/DetMath.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace evo {

const char* actionName(Action a) {
    switch (a) {
        case Action::Rest: return "rest";
        case Action::Forage: return "forage";
        case Action::Explore: return "explore";
        case Action::Flee: return "flee";
        case Action::Hunt: return "hunt";
        case Action::SeekMate: return "seek-mate";
        case Action::Social: return "social";
        case Action::AvoidHazard: return "avoid-hazard";
        case Action::Steer: return "steer";
        case Action::kCount: break;
    }
    return "?";
}

const char* deathCauseName(DeathCause c) {
    switch (c) {
        case DeathCause::None: return "alive";
        case DeathCause::Starvation: return "starvation";
        case DeathCause::OldAge: return "old-age";
        case DeathCause::Predation: return "predation";
        case DeathCause::Culled: return "culled";
    }
    return "?";
}

Vec2 jitterDirection(Vec2 heading, double sd, Rng& rng) {
    Vec2 h = heading.lengthSq() > 1e-12 ? heading.normalized() : Vec2{1.0, 0.0};
    const Vec2 perp{-h.y, h.x};
    Vec2 d = h + perp * rng.normal(0.0, sd);
    return d.normalized();
}

Intent UtilityController::decide(const Organism& o, const Perception& p, Rng& rng) const {
    const Genome& g = o.genome;
    const double h = p.hunger;
    const double caution = 1.0 - g[RiskTolerance];
    const bool predator = o.kind == Kind::Predator;

    // Candidate (score, intent). Order is fixed; ties keep the earlier one.
    Intent best;
    double bestScore = -1.0;
    auto consider = [&](double score, Action a, Vec2 dir, double speed) {
        if (score > bestScore) {
            bestScore = score;
            best = Intent{a, dir, std::clamp(speed, 0.0, 1.0)};
        }
    };

    consider(g[WRest] * (1.0 - h) * 0.8, Action::Rest, Vec2{}, 0.0);

    if (!predator && p.foodSeen) {
        consider(g[WForage] * (0.3 + h) * (0.5 + 0.5 * p.foodAmount), Action::Forage, p.foodDir,
                 o.pheno.maxSpeed > 0 ? p.foodDist / o.pheno.maxSpeed : 1.0);
    }
    if (predator && p.preySeen) {
        consider(g[WForage] * (0.3 + h) * 1.2, Action::Hunt, p.preyDir, 1.0);
    }
    {
        const double searching = (predator ? p.preySeen : p.foodSeen) ? 0.3 : 1.0;
        consider(g[WExplore] * (0.25 + 0.75 * h) * searching, Action::Explore,
                 jitterDirection(o.heading, 0.35, rng), 0.6);
    }
    if (p.threatSeen) {
        consider(g[WFlee] * caution * (1.5 - p.threatDist / std::max(1.0, p.vision)) * 1.5, Action::Flee,
                 p.threatDir * -1.0, 1.0);
    }
    if (p.hazardSeen) {
        consider(g[WFlee] * caution * p.hazardProximity * 1.5, Action::AvoidHazard, p.hazardAway, 0.8);
    }
    if (sexual_ && p.readyToMate && p.mateSeen) {
        consider(g[WMate] * (0.5 + p.ageFrac), Action::SeekMate, p.mateDir,
                 o.pheno.maxSpeed > 0 ? p.mateDist / o.pheno.maxSpeed : 1.0);
    }
    if (p.neighbours > 0 && std::fabs(g[Social]) > 1e-9) {
        const Vec2 dir = g[Social] > 0 ? p.neighbourDir : p.neighbourDir * -1.0;
        consider(std::fabs(g[Social]) * 0.4, Action::Social, dir, 0.5);
    }
    return best;
}

Intent NeuralController::decide(const Organism& o, const Perception& p, Rng&) const {
    // Inputs are bounded to roughly [-1, 1].
    double in[kInputs];
    auto scaled = [&](bool seen, Vec2 dir, double dist) {
        const double s = seen ? 1.0 - dist / std::max(1.0, p.vision) : 0.0;
        return Vec2{dir.x * s, dir.y * s};
    };
    const Vec2 food = scaled(p.foodSeen, p.foodDir, p.foodDist);
    const Vec2 threat = p.threatSeen ? scaled(true, p.threatDir, p.threatDist)
                                     : (p.hazardSeen ? p.hazardAway * (-p.hazardProximity) : Vec2{});
    const Vec2 prey = scaled(p.preySeen, p.preyDir, p.preyDist);
    const Vec2 mate = scaled(p.mateSeen, p.mateDir, p.mateDist);
    in[0] = p.hunger;
    in[1] = food.x; in[2] = food.y;
    in[3] = threat.x; in[4] = threat.y;
    in[5] = prey.x; in[6] = prey.y;
    in[7] = mate.x; in[8] = mate.y;
    in[9] = det::tanh(p.neighbours / 8.0);
    in[10] = det::tanh(p.tempMismatch);
    in[11] = p.readyToMate ? 1.0 : 0.0;
    in[12] = 1.0;  // bias

    const double* w = o.genome.brain();
    if (o.genome.brainSize() < genomeExtraSize()) throw std::logic_error("genome lacks neural weights");
    double hid[64];
    for (int j = 0; j < hidden_; ++j) {
        double s = 0.0;
        for (int i = 0; i < kInputs; ++i) s += w[j * kInputs + i] * in[i];
        hid[j] = det::tanh(s);
    }
    const double* w2 = w + kInputs * hidden_;
    double out[kOutputs];
    for (int k = 0; k < kOutputs; ++k) {
        double s = w2[k * (hidden_ + 1) + hidden_];  // bias
        for (int j = 0; j < hidden_; ++j) s += w2[k * (hidden_ + 1) + j] * hid[j];
        out[k] = s;
    }
    Intent it;
    it.action = Action::Steer;
    it.dir = Vec2{det::tanh(out[0]), det::tanh(out[1])}.normalized();
    it.speedFrac = 1.0 / (1.0 + det::exp(-out[2]));
    if (it.dir.lengthSq() < 1e-12) it.speedFrac = 0.0;
    if (it.speedFrac < 0.05) it.action = Action::Rest;
    return it;
}

std::unique_ptr<Controller> makeController(const std::string& type, int neuralHidden, bool sexual) {
    if (type == "neural") return std::make_unique<NeuralController>(neuralHidden);
    return std::make_unique<UtilityController>(sexual);
}

}  // namespace evo
