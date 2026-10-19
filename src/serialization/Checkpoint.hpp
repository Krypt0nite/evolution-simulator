#pragma once
// Binary, versioned snapshot of the complete simulation state. Resuming from a
// checkpoint continues bit-identically to an uninterrupted run (tested).
//
// Layout: "EVOCKPT\0" | u32 format | str engineVersion | str configJson | state…
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/Simulation.hpp"

namespace evo {

class CheckpointError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Checkpoint {
public:
    static constexpr std::uint32_t kFormatVersion = 1;

    static std::vector<std::uint8_t> save(const Simulation& sim);
    static std::unique_ptr<Simulation> load(const std::vector<std::uint8_t>& bytes);

    static void saveFile(const Simulation& sim, const std::string& path);
    static std::unique_ptr<Simulation> loadFile(const std::string& path);
};

}  // namespace evo
