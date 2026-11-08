#pragma once
#include <cstddef>
#include <cstdint>

namespace steamohos {
struct NativeExecRange { uintptr_t address, bytes; };
// No allocation while enumerating /proc/self/maps. Adjacent reclaimable VMAs
// can share one munmap even when their permissions/backing files differ.
// A retained interval or an unmapped gap must never be crossed.
class NativeExecRanges {
public:
    NativeExecRanges(NativeExecRange* storage, size_t capacity) : ranges_(storage), capacity_(capacity) {}
    bool Append(uintptr_t first, uintptr_t last) {
        if (first > last) return false;
        if (first == last) return true;
        if (count_) {
            auto& previous = ranges_[count_ - 1];
            const uintptr_t end = previous.address + previous.bytes;
            if (first < end) return false;
            if (first == end) {
                previous.bytes = last - previous.address;
                ++segments_; bytes_ += last - first;
                return true;
            }
        }
        if (count_ == capacity_) return false;
        ranges_[count_++] = {first, last - first};
        ++segments_; bytes_ += last - first;
        return true;
    }
    size_t Count() const { return count_; }
    size_t Segments() const { return segments_; }
    size_t Bytes() const { return bytes_; }
private:
    NativeExecRange* ranges_;
    size_t capacity_, count_ = 0, segments_ = 0, bytes_ = 0;
};
}
