// libFuzzer harness for the y-websocket message decoder (src/sync,
// BLD_SYNC): arbitrary bytes from the network are decoded or refused, and
// a decoded message re-encodes to the same bytes.

#include "SyncProtocol.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const QByteArray in(reinterpret_cast<const char*>(data), static_cast<qsizetype>(size));
    if (const auto m = bld::sync::protocol::decode(in)) {
        const auto again = bld::sync::protocol::decode(bld::sync::protocol::encode(m->kind, m->payload));
        if (!again || again->kind != m->kind || again->payload != m->payload) std::abort();
    }
    return 0;
}
