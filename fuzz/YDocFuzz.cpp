// libFuzzer harness for the sync document reader (src/sync, BLD_SYNC):
// arbitrary bytes as a Yjs update must be refused or read, never crash.
// Seed: fixtures/sync/*.ydoc.

#include "YDocSummary.h"

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    (void)bld::sync::summarizeDoc(QByteArray(reinterpret_cast<const char*>(data), static_cast<qsizetype>(size)));
    return 0;
}
