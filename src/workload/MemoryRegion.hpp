#ifndef MEMTEST_WORKLOAD_MEMORYREGION_HPP
#define MEMTEST_WORKLOAD_MEMORYREGION_HPP

#include <cstdint>

namespace memtest {

// Region anonima privada con mmap. El destructor hace munmap; si la reserva
// falla, `valid()` es false y `error_code()` conserva errno (ENOMEM cuando el
// limite del sistema operativo la impide).
class MemoryRegion {
public:
    explicit MemoryRegion(std::uint64_t bytes);
    ~MemoryRegion();

    MemoryRegion(const MemoryRegion&) = delete;
    MemoryRegion& operator=(const MemoryRegion&) = delete;

    bool valid() const { return data_ != nullptr; }
    void* data() const { return data_; }
    std::uint64_t size() const { return size_; }
    int error_code() const { return error_code_; }

private:
    void* data_ = nullptr;
    std::uint64_t size_ = 0;
    int error_code_ = 0;
};

}  // namespace memtest

#endif  // MEMTEST_WORKLOAD_MEMORYREGION_HPP
