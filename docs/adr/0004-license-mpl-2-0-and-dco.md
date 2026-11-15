# 0004. License: MPL-2.0, contributions under DCO

- **Status:** Accepted
- **Date:** 2026-11-15

## Context

ezMU should be open: improvements to the engine should flow back to the community. At the
same time, developers compile the engine together with their own game code into one final
executable (typically statically linked) and many want to keep their game code private.

- **GPL-3.0** would force every distributed client built with ezMU to be open source.
- **LGPL-3.0** allows closed games only with dynamic linking (or shipping relinkable object
  files), and its treatment of templates and inline header code is ambiguous for a C++ engine.
- **Permissive licenses** (MIT/zlib) do not require engine improvements to be shared.

## Decision

We will license ezMU under the **Mozilla Public License 2.0** (file-level copyleft):

- Files of ezMU that are modified and distributed must be made available under MPL-2.0.
- New files written by a game developer may use any license, including proprietary.
- Static linking, templates and headers are fine; the unit of copyleft is the file.

Contributions are accepted under the **Developer Certificate of Origin 1.1**, certified by a
`Signed-off-by` line in every commit (`git commit -s`). No CLA is required.

## Consequences

- Game developers can ship closed clients built on ezMU; engine fixes they make stay open.
- MPL-2.0 is compatible with GPL, so GPL games may be built on ezMU.
- Relicensing later would require consent from all contributors; DCO records who they are.
- Third-party dependencies must have licenses compatible with MPL-2.0 distribution.
- Legal review is recommended before the first public release.

## Alternatives considered

- **GPL-3.0** — too restrictive for the intended ecosystem of independent game clients.
- **LGPL-3.0** — awkward with static linking and template-heavy C++.
- **MIT/zlib** — no obligation to share engine improvements.
