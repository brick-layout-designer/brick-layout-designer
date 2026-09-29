// libFuzzer harness for the sync document reader (src/sync, BLD_SYNC):
// arbitrary bytes as a Yjs update must be refused or read, and read
// documents mapped onto a layout or refused, never crash.
// Seed: fixtures/sync/*.ydoc.

#include "DocJson.h"
#include "WebModel.h"
#include "YDocSummary.h"

#include "core/Map.h"

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const QByteArray update(reinterpret_cast<const char*>(data), static_cast<qsizetype>(size));
    (void)bld::sync::summarizeDoc(update);
    if (const auto json = bld::sync::docToJson(update)) (void)bld::sync::mapFromDocJson(*json);
    return 0;
}
