#include "core/ResultStore.hpp"

#include <utility>

namespace memtest {

void ResultStore::add(ExperimentResult result) {
    results_.push_back(std::move(result));
}

const ExperimentResult* ResultStore::find(const std::string& run_id) const {
    for (const ExperimentResult& result : results_) {
        if (result.run_id == run_id) {
            return &result;
        }
    }
    return nullptr;
}

}  // namespace memtest
