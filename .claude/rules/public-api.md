# Public API

Everything under `include/ezmu/<module>/` is a contract with game developers, tools and the
Framework. Treat changes to it as more expensive than changes to `src/`.

- Expose ezMU types only. Third-party types stay inside the owning module (ADR 0008), unless its
  ADR explicitly allows re-export (e.g. math types from foundation).
- Keep headers light: forward-declare where possible; no heavy includes for convenience.
- Document every public type and function with a short `///` comment: purpose, ownership,
  lifetime, thread-safety and error behavior.
- Prefer small, composable interfaces (`IAssetImporter`, `IProtocolAdapter`) for extension points.
- Use handles and value types across boundaries; avoid exposing internal containers.
- Thread-safety must be explicit: state which thread(s) may call each function.
- Breaking a public API requires updating all in-repo users (Framework, tools, samples) in the same
  change and noting it in the commit message (`feat(x)!:` or a `BREAKING CHANGE:` footer).
- New public API needs tests that use it the way a game developer would.
