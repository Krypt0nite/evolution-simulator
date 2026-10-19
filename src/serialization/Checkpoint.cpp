#include "serialization/Checkpoint.hpp"

#include <cstring>
#include <fstream>
#include <iterator>
#include <type_traits>

namespace evo {

namespace {

constexpr char kMagic[8] = {'E', 'V', 'O', 'C', 'K', 'P', 'T', '\0'};

class Writer {
public:
    template <typename T>
    void pod(const T& v) {
        static_assert(std::is_trivially_copyable_v<T>);
        const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
        buf.insert(buf.end(), p, p + sizeof(T));
    }
    void str(const std::string& s) {
        pod(static_cast<std::uint64_t>(s.size()));
        buf.insert(buf.end(), s.begin(), s.end());
    }
    template <typename T>
    void podVec(const std::vector<T>& v) {
        static_assert(std::is_trivially_copyable_v<T>);
        pod(static_cast<std::uint64_t>(v.size()));
        if (!v.empty()) {
            const auto* p = reinterpret_cast<const std::uint8_t*>(v.data());
            buf.insert(buf.end(), p, p + sizeof(T) * v.size());
        }
    }
    std::vector<std::uint8_t> buf;
};

class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& b) : buf(b) {}
    template <typename T>
    T pod() {
        need(sizeof(T));
        T v;
        std::memcpy(&v, buf.data() + pos, sizeof(T));
        pos += sizeof(T);
        return v;
    }
    std::string str() {
        const auto n = pod<std::uint64_t>();
        need(n);
        std::string s(reinterpret_cast<const char*>(buf.data() + pos), static_cast<std::size_t>(n));
        pos += static_cast<std::size_t>(n);
        return s;
    }
    template <typename T>
    std::vector<T> podVec() {
        const auto n = pod<std::uint64_t>();
        if (n > (buf.size() - pos) / sizeof(T)) throw CheckpointError("checkpoint truncated or corrupt");
        std::vector<T> v(static_cast<std::size_t>(n));
        if (n) std::memcpy(v.data(), buf.data() + pos, sizeof(T) * static_cast<std::size_t>(n));
        pos += sizeof(T) * static_cast<std::size_t>(n);
        return v;
    }
    bool atEnd() const { return pos == buf.size(); }

private:
    void need(std::uint64_t n) const {
        if (n > buf.size() - pos) throw CheckpointError("checkpoint truncated or corrupt");
    }
    const std::vector<std::uint8_t>& buf;
    std::size_t pos = 0;
};

void writeEvent(Writer& w, const EnvEvent& e) {
    w.pod(e.tick); w.str(e.type); w.pod(e.value); w.pod(e.pos); w.pod(e.radius); w.pod(e.damage); w.pod(e.count);
}
EnvEvent readEvent(Reader& r) {
    EnvEvent e;
    e.tick = r.pod<int>(); e.type = r.str(); e.value = r.pod<double>(); e.pos = r.pod<Vec2>();
    e.radius = r.pod<double>(); e.damage = r.pod<double>(); e.count = r.pod<int>();
    return e;
}

void writeOrganism(Writer& w, const Organism& o) {
    w.pod(o.id); w.pod(o.parentA); w.pod(o.parentB); w.pod(o.founder); w.pod(o.generation);
    w.pod(o.kind); w.pod(o.alive); w.pod(o.cause);
    w.podVec(o.genome.genes);
    w.pod(o.pos); w.pod(o.heading); w.pod(o.energy); w.pod(o.age); w.pod(o.cooldown); w.pod(o.handling);
    w.pod(o.offspring); w.pod(o.cluster); w.pod(o.action); w.pod(o.lastMoved);
    w.pod(o.recent); w.pod(o.recentHead);
}
Organism readOrganism(Reader& r, const SimulationConfig& cfg) {
    Organism o;
    o.id = r.pod<std::uint64_t>(); o.parentA = r.pod<std::uint64_t>(); o.parentB = r.pod<std::uint64_t>();
    o.founder = r.pod<std::uint64_t>(); o.generation = r.pod<std::uint32_t>();
    o.kind = r.pod<Kind>(); o.alive = r.pod<bool>(); o.cause = r.pod<DeathCause>();
    o.genome.genes = r.podVec<double>();
    if (o.genome.genes.size() < kNumTraits) throw CheckpointError("checkpoint organism genome too short");
    o.pheno = express(o.genome, o.kind, cfg);
    o.pos = r.pod<Vec2>(); o.heading = r.pod<Vec2>(); o.energy = r.pod<double>();
    o.age = r.pod<std::uint32_t>(); o.cooldown = r.pod<std::uint32_t>(); o.handling = r.pod<std::uint32_t>(); o.offspring = r.pod<std::uint32_t>();
    o.cluster = r.pod<std::int32_t>(); o.action = r.pod<Action>(); o.lastMoved = r.pod<double>();
    o.recent = r.pod<std::array<Action, 8>>(); o.recentHead = r.pod<std::uint8_t>();
    return o;
}

}  // namespace

std::vector<std::uint8_t> Checkpoint::save(const Simulation& s) {
    Writer w;
    for (char c : kMagic) w.pod(c);
    w.pod(kFormatVersion);
    w.str(kEngineVersion);
    // Effective config, including any interactively scheduled events.
    w.str(s.cfg_.toJson().dump());

    w.pod(s.tick_);
    w.pod(s.nextId_);
    w.pod(static_cast<std::uint64_t>(s.eventCursor_));
    w.pod(static_cast<std::uint64_t>(s.events_.size()));
    for (const auto& e : s.events_) writeEvent(w, e);
    w.str(s.rng_.serialize());

    w.pod(static_cast<std::uint64_t>(s.organisms_.size()));
    for (const auto& o : s.organisms_) writeOrganism(w, o);

    w.podVec(s.food_.biomassData());
    w.podVec(s.food_.patches());
    w.pod(s.food_.multiplier());
    w.pod(s.food_.regrowthRate());

    w.pod(s.env_.offset());
    w.podVec(s.env_.hazards());

    w.podVec(s.lineage_.records());
    w.pod(s.lineage_.truncated());

    w.pod(s.species_.nextId());
    w.pod(static_cast<std::uint64_t>(s.species_.clusters().size()));
    for (const auto& c : s.species_.clusters()) {
        w.pod(c.id); w.pod(c.size); w.pod(c.preySize); w.pod(c.predatorSize);
        w.podVec(c.centroid);
    }

    const Statistics& st = s.stats_;
    w.podVec(st.samples_);
    w.pod(st.births_); w.pod(st.deaths_); w.pod(st.starv_); w.pod(st.old_); w.pod(st.pred_); w.pod(st.suppressed_);
    w.podVec(st.deathAges_);
    w.pod(st.offspringSum_);
    w.pod(st.totalBirths_); w.pod(st.totalDeaths_);

    w.pod(s.ledger_);
    return std::move(w.buf);
}

std::unique_ptr<Simulation> Checkpoint::load(const std::vector<std::uint8_t>& bytes) {
    Reader r(bytes);
    char magic[8];
    for (char& c : magic) c = r.pod<char>();
    if (std::memcmp(magic, kMagic, 8) != 0) throw CheckpointError("not an EvoLab checkpoint (bad magic)");
    const auto fmt = r.pod<std::uint32_t>();
    if (fmt != kFormatVersion)
        throw CheckpointError("checkpoint format v" + std::to_string(fmt) + " is not supported (expects v" +
                              std::to_string(kFormatVersion) + ")");
    const std::string engine = r.str();
    if (engine != kEngineVersion)
        throw CheckpointError("checkpoint was written by engine " + engine + "; this is engine " + kEngineVersion +
                              ". Re-run from its config + seed instead.");
    SimulationConfig cfg = SimulationConfig::fromJsonText(r.str());
    auto sim = std::make_unique<Simulation>(cfg);
    Simulation& s = *sim;

    s.tick_ = r.pod<std::uint64_t>();
    s.nextId_ = r.pod<std::uint64_t>();
    s.eventCursor_ = static_cast<std::size_t>(r.pod<std::uint64_t>());
    const auto nEvents = r.pod<std::uint64_t>();
    s.events_.clear();
    for (std::uint64_t i = 0; i < nEvents; ++i) s.events_.push_back(readEvent(r));
    if (s.eventCursor_ > s.events_.size()) throw CheckpointError("checkpoint event cursor out of range");
    s.rng_.deserialize(r.str());

    const auto nOrg = r.pod<std::uint64_t>();
    s.organisms_.clear();
    s.organisms_.reserve(static_cast<std::size_t>(nOrg));
    for (std::uint64_t i = 0; i < nOrg; ++i) s.organisms_.push_back(readOrganism(r, s.cfg_));

    auto biomass = r.podVec<double>();
    if (biomass.size() != s.food_.cellCount()) throw CheckpointError("checkpoint food grid size mismatch");
    s.food_.mutableBiomass() = std::move(biomass);
    auto patches = r.podVec<Vec2>();
    const double mult = r.pod<double>();
    const double regrow = r.pod<double>();
    s.food_.restore(patches, mult, regrow, s.tick_ > 0 ? s.tick_ - 1 : 0);

    s.env_.setOffset(r.pod<double>());
    s.env_.hazards() = r.podVec<Hazard>();
    s.env_.update(s.tick_);

    s.lineage_.mutableRecords() = r.podVec<LineageRecord>();
    s.lineage_.setTruncated(r.pod<bool>());

    s.species_.setNextId(r.pod<int>());
    const auto nClusters = r.pod<std::uint64_t>();
    auto& clusters = s.species_.mutableClusters();
    clusters.clear();
    for (std::uint64_t i = 0; i < nClusters; ++i) {
        GeneticCluster c;
        c.id = r.pod<int>(); c.size = r.pod<int>(); c.preySize = r.pod<int>(); c.predatorSize = r.pod<int>();
        c.centroid = r.podVec<double>();
        clusters.push_back(std::move(c));
    }

    Statistics& st = s.stats_;
    st.samples_ = r.podVec<Sample>();
    st.births_ = r.pod<int>(); st.deaths_ = r.pod<int>(); st.starv_ = r.pod<int>(); st.old_ = r.pod<int>();
    st.pred_ = r.pod<int>(); st.suppressed_ = r.pod<int>();
    st.deathAges_ = r.podVec<std::uint32_t>();
    st.offspringSum_ = r.pod<double>();
    st.totalBirths_ = r.pod<std::uint64_t>(); st.totalDeaths_ = r.pod<std::uint64_t>();

    s.ledger_ = r.pod<EnergyLedger>();
    s.gridValid_ = false;
    if (!r.atEnd()) throw CheckpointError("checkpoint has trailing data (corrupt?)");
    return sim;
}

void Checkpoint::saveFile(const Simulation& sim, const std::string& path) {
    const auto bytes = save(sim);
    std::ofstream f(path, std::ios::binary);
    if (!f) throw CheckpointError("cannot write " + path);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::unique_ptr<Simulation> Checkpoint::loadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw CheckpointError("cannot open " + path);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return load(bytes);
}

}  // namespace evo
