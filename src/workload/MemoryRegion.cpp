#include "workload/MemoryRegion.hpp"

#include <cerrno>
#include <cstddef>

#include <sys/mman.h>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

namespace memtest {

MemoryRegion::MemoryRegion(std::uint64_t bytes) : size_(bytes) {
    if (bytes == 0) {
        return;
    }

    void* mapped = ::mmap(nullptr, static_cast<std::size_t>(bytes),
                          PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
                          -1, 0);
    if (mapped == MAP_FAILED) {
        error_code_ = errno;
        return;
    }
    data_ = mapped;
}

MemoryRegion::~MemoryRegion() {
    if (data_ != nullptr) {
        ::munmap(data_, static_cast<std::size_t>(size_));
    }
}

}  // namespace memtest
