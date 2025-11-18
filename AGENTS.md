# Repository Guidelines

Venus Plus pairs a network-ready Vulkan ICD (`client/`) with a GPU-backed server (`server/`). Read these notes before touching code so every agent makes consistent, incremental progress.

## Project Structure & Module Organization
- `common/` holds shared protocol helpers, networking, and utilities; keep wire-format changes here so both sides stay in sync.
- `client/` implements the ICD entrypoints plus per-phase logic under `commands/` and `state/`; `server/` mirrors that layout with `decoder/`, `executor/`, and `state/`.
- `test-app/` provides end-to-end Vulkan scenarios (`phase01/` …) that exercise new features; update the relevant phase whenever client and server coordination changes.
- `docs/` captures phase planning, architecture, and troubleshooting. Refresh or link to these docs when the directory layout or protocol surface shifts.

## Build, Test, and Development Commands
- Configure and build with `cmake -S . -B build && cmake --build build` (or append `--target venus-server` / `venus_icd` while iterating).
- Run the server: `./build/server/venus-server --port 5556`.
- Run the test app via the ICD: `VK_DRIVER_FILES=$PWD/build/client/venus_icd.x86_64.json ./build/test-app/venus-test-app --phase 1` (bump the phase or use `--phase all` when validating regressions).
- Unit tests live under each module; trigger them with `ctest --test-dir build/client/tests`, `build/server/tests`, and `build/common/tests`.

## Coding Style & Naming Conventions
Use C++17, four-space indentation, and brace-on-same-line (see `client/icd/icd_entrypoints.cpp`). File names stay snake_case, classes use `PascalCase`, and namespaces remain `venus_plus`. Headers rely on `#ifndef VENUS_PLUS_*` guards; prefer RAII helpers over raw pointers, and document cross-process behavior with short comments when not obvious.

## Testing Guidelines
Every feature needs a matching gtest (`TEST(Fixture, Behavior)`) in the owning module plus an integration or phase test if it spans client/server boundaries. Keep wire protocol changes covered by round-trip tests in `common/tests/` and refresh `test-app` phases so end-to-end runs still pass. Failures should reproduce with `ctest --output-on-failure` and a single `--phase` invocation before review.

## Commit & Pull Request Guidelines
History shows concise, descriptive titles (e.g., `Phase 2`, `Venus protocol integration is complete and working`). Follow that tone: imperative, <=72 chars, mention the touched area (`server: add queue executor`). PRs should link the relevant phase or issue, list build/test commands you ran, describe protocol or ABI changes, and include logs or screenshots when demonstrating new behavior.

## Security & Configuration Tips
Network ports default to loopback (`127.0.0.1:5556`); never hard-code production hosts. Keep credentials out of logs, and gate experimental toggles behind env vars (e.g., future `VENUS_PLUS_SERVER`). When testing remote GPUs, pin `VK_DRIVER_FILES` to the built manifest and document any non-default firewall or udev tweaks in `docs/TROUBLESHOOTING.md`.

## ⚠️ Critical ICD Implementation Requirements

**Symbol Visibility Control** - This is MANDATORY and causes infinite recursion crashes if violated:

- **Only `vk_icd*` functions should be exported** (`VP_PUBLIC`):
  - `vk_icdNegotiateLoaderICDInterfaceVersion`
  - `vk_icdGetInstanceProcAddr`
  - `vk_icdGetPhysicalDeviceProcAddr`

- **All Vulkan functions MUST be hidden** (`VP_PRIVATE`):
  - `vkCreateInstance`, `vkDestroyDevice`, `vkGetDeviceQueue`, etc.
  - If these are exported, the loader finds ICD symbols directly instead of using the dispatch table, causing infinite recursion

- **CMake must set default visibility to hidden**:
  ```cmake
  set_target_properties(venus_icd PROPERTIES
      CXX_VISIBILITY_PRESET hidden
      C_VISIBILITY_PRESET hidden
  )
  ```

- **Verify with**: `nm -D client/libvenus_icd.so | grep " T " | grep vk`
  - Should show ONLY `vk_icd*` functions
  - Should NOT show `vkCreateInstance`, `vkGetDeviceQueue`, etc.

**Handle Structures** - VkDevice and VkQueue must be pointers to structs with `void* loader_data` as the FIRST member. The loader writes its dispatch table there.
