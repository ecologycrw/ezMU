# Workflow

Source: ADR 0019.

## Per feature

1. **Spec** — non-trivial features start with `docs/specs/<name>.md` from the template; the owner
   reviews it before implementation.
2. **Plan** — break work into small, independently verifiable steps.
3. **Implement** — one step at a time, with tests.
4. **Verify** — `./scripts/dev.ps1` (the Stop hook also enforces build + tests). Use
   `./scripts/check.ps1 -Tidy` before committing larger changes.
5. **Commit** — small Conventional Commit, signed off (`git commit -s`), scope = module.
6. **Record** — if a decision was made, add an ADR and update `docs/adr/README.md`.

## Creating a module

- `engine/<name>/` (or `framework/<name>/`) with `include/ezmu/<name>/`, `src/`, `tests/`,
  `CMakeLists.txt` using `ezmu_add_module` and `ezmu_add_module_tests`, and a module `CLAUDE.md`
  (responsibility, public API, allowed dependencies, pitfalls).
- Register it in the parent `CMakeLists.txt` in layer order.
- Add path-scoped rules in `.claude/rules/` if the module has its own constraints.
- Add any new third-party library to `vcpkg.json` and to ADR 0008.

## Things that need the owner

Visual quality and game feel, public API shape for game developers, new dependencies, anything
that changes an accepted ADR, and pushing to the remote.
