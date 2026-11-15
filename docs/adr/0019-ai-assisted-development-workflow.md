# 0019. AI-assisted development workflow

- **Status:** Accepted
- **Date:** 2026-11-15

## Context

ezMU is developed primarily by an AI coding agent (Claude Code), with the project owner
providing direction, architectural decisions, reviews and feedback on what tests cannot measure
(visual quality, game feel). An AI agent writes code quickly but does not retain project memory
across sessions and can only be as reliable as its ability to verify its own work.

## Decision

The project invests in agent infrastructure in this priority order:

1. **Automated verification** — fast incremental builds, warnings as errors, unit tests,
   Vulkan validation layers failing tests, sanitizers, clang-tidy, headless rendering tests
   with reference images and frame capture, architecture checks, CI on every change.
2. **Written knowledge** — a short root `CLAUDE.md`; per-module `CLAUDE.md`; ADRs; specs written
   before implementation; global and path-scoped rules in `.claude/rules/`.
3. **Hooks** (`.claude/settings.json`) — deterministic guarantees: formatting after edits,
   protected paths, build and tests before finishing a task.
4. **Skills** — created only when a workflow has repeated about three times
   (e.g. "create a module", "add a domain message", "write an ADR").
5. **Subagents** — e.g. architecture reviewer, public API reviewer, test writer — once the
   architecture they check against is documented.

Rules of thumb:

- If something must **never** happen, it becomes a hook or a CI check, not only an instruction.
- Critical rules live in both places: a rule explains *why/how*; a check *enforces*.
- Workflow per feature: **spec → plan → small implementation steps → automated verification →
  review → small commit (+ ADR if a decision was made)**.

## Consequences

- The owner's time goes to direction and judgment rather than to catching mechanical errors.
- The repository itself is the project's memory; undocumented decisions effectively do not
  exist.
- Infrastructure grows with the project instead of being built speculatively.

## Alternatives considered

- **Ad-hoc prompting without project infrastructure** — inconsistent results across sessions.
- **Building every skill and agent upfront** — documents processes that do not exist yet.
