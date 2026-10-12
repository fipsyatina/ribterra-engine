# Sanitizer coverage

`headless-ubsan` instruments project code with `-fsanitize=undefined` and stops at the first diagnostic. The CI suite includes simulation, persistence, validation and NetworkSessionTests. Prebuilt vcpkg dependencies are not instrumented.

The pinned GameNetworkingSockets 1.6.0 implementation uses `-fno-rtti` unless its own SANITIZE_UNDEFINED build option is enabled ([pinned upstream source](https://github.com/ValveSoftware/GameNetworkingSockets/blob/2cb93a06350bb065db53abdb0d87cf297e0bfd34/src/CMakeLists.txt#L167)). The virtual C++ interface therefore lacks the RTTI metadata required by the vptr check; this incompatibility is also described by the [UBSan documentation](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html#available-checks).

Only `NetTransport` (the single `src/net/gns_transport.cpp` adapter) adds `-fno-sanitize=vptr` in sanitizer builds. Other undefined checks remain enabled in that adapter. Protocol, NetworkSession, GameServer, SimCore and the tests retain their normal instrumentation. Ordinary Windows/Linux builds are unaffected. This exception does not provide lifetime/type checking across the GNS virtual interface, or sanitizer coverage of GNS itself.

The first CI run that added NetworkSessionTests exposed this boundary at CreateListenSocketIP before gameplay began: [failure excerpt](evidence/release0101-ubsan-initial-failure.txt), [diagnosis](evidence/release0101-ubsan-boundary.json). Subsequent results belong to [VALIDATION](VALIDATION.md); the failed run remains recorded.
