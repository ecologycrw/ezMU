# Contributing to ezMU

Thank you for your interest in ezMU. This document describes how changes are proposed,
reviewed and accepted.

## Before you start

- Read the [architecture overview](docs/architecture/overview.md) and the
  [Architecture Decision Records](docs/adr/README.md). Changes that contradict an accepted ADR
  need a new ADR that supersedes it.
- Non-trivial features start with a spec in `docs/specs/` (see the
  [template](docs/specs/_template.md)), reviewed before implementation.

## Workflow

- **Trunk-based development.** `main` must always build and pass all tests.
- Work on short-lived branches and open a pull request against `main`.
- CI must pass on every platform before a pull request is merged.
- Keep pull requests small and focused on one change.

## Commit messages

Commits follow [Conventional Commits](https://www.conventionalcommits.org/):

```
<type>(<scope>): <summary>

feat(renderer): add terrain pass
fix(net): handle partial frames
docs(adr): add ADR 0020 for UI layout format
```

Common types: `feat`, `fix`, `docs`, `refactor`, `perf`, `test`, `build`, `ci`, `chore`.
The scope is usually the module name.

## Developer Certificate of Origin (DCO)

Every commit must be signed off, certifying that you wrote the change or have the right to
submit it under the project's license, as described in the
[Developer Certificate of Origin 1.1](https://developercertificate.org/).

Sign off by adding `-s` to your commit command:

```bash
git commit -s -m "feat(foundation): add result type"
```

This appends a `Signed-off-by: Your Name <your@email>` line using your git identity.

## Code style

- Formatting is defined by `.clang-format`; run it before committing (CI rejects unformatted code).
- Static analysis is configured in `.clang-tidy`.
- Naming and other conventions are defined in
  [ADR 0018](docs/adr/0018-code-commit-and-branch-conventions.md).

## License

By contributing, you agree that your contributions are licensed under the
[Mozilla Public License 2.0](LICENSE).
