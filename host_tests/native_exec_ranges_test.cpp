#include "../entry/src/main/cpp/linux_steam/native_exec_ranges.h"
#include <cassert>
#include <iostream>
int main() {
    using namespace steamohos;
    NativeExecRange ranges[2]; NativeExecRanges inventory(ranges, 2);
    // A fragmented FEX arena may contain more VMAs than the old 16384 limit.
    for (uintptr_t n = 1; n <= 20000; ++n) assert(inventory.Append(n*4096, (n+1)*4096));
    assert(inventory.Count() == 1 && inventory.Segments() == 20000);
    assert(ranges[0].address == 4096 && ranges[0].bytes == 20000*4096);
    assert(inventory.Bytes() == 20000*4096);
    // Retained page at 20001*4096 stays outside both cleanup ranges.
    assert(inventory.Append(20002*4096, 20003*4096));
    assert(inventory.Count() == 2);
    // Coalesce even at capacity, but never bridge a retained or unmapped gap.
    assert(inventory.Append(20003*4096, 20004*4096));
    assert(!inventory.Append(20005*4096, 20006*4096));
    assert(!inventory.Append(20002*4096, 20003*4096));
    assert(!inventory.Append(4, 3)); assert(inventory.Append(0, 0));
    assert(ranges[1].address == 20002*4096 && ranges[1].bytes == 2*4096);
    assert(inventory.Bytes() == 20002*4096 && inventory.Segments() == 20002);
    std::cout << "PASS: >16384 adjacent VMAs, retained gaps, capacity, overlap and empty intervals\n";
}
