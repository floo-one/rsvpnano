# G-Stack installation and workflow

Verified 2026-10-18: official checkout `/Users/floo/.claude/skills/gstack`, release **1.91.125.0**, commit `c10c139ed6472ea965f0be4af1567aa580100d7f`. An existing Claude installation was preserved and fast-forwarded; Codex was added using official setup, then Claude renders refreshed. No skill files were copied into this repository.

Source: [official README](https://github.com/garrytan/gstack#other-ai-agents). Commands used from the checkout:

```sh
./setup --host codex --team
./setup --host claude --team
./setup --status
```

From this project, the official `bin/gstack-team-init required` generated `CLAUDE.md`, `.claude/hooks/check-gstack.cjs` and `.claude/settings.json`. The narrow `.gitignore` exceptions allow those files to be committed while retaining private/generated runtime exclusions. The hook's Node syntax and installed-root success path are checked. Team auto-update is a **Claude session hook**; do not claim Codex executes Claude hooks. Codex support is documented as experimental and safety hooks advisory. Global renders are installed under `~/.codex/skills/gstack-*`; start a fresh agent session to refresh catalog discovery. This session explicitly read the files from disk.

All eight requested skills resolve to readable installed `SKILL.md` files:

| Codex skill | Initialization use |
|---|---|
| `gstack-office-hours` | Builder discovery; A selected by user; independent local spec review passed |
| `gstack-plan-ceo-review` | Hold-scope planning review; findings and unresolved decisions documented |
| `gstack-plan-design-review` | Draft plan design assessment; physical/mockup approval still pending |
| `gstack-plan-eng-review` | Draft architecture/test/performance assessment; implementation clearance pending |
| `gstack-review` | Installed; no product implementation review claimed |
| `gstack-qa` | Installed; not a substitute for ESP32 hardware testing |
| `gstack-investigate` | Installed; available for later root-cause work |
| `gstack-ship` | Installed; not run, no push/PR/release authorized |

`./setup --status` reports both Claude and Codex current, real-file routers and all checked section links resolving (65 Claude, 25 Codex). This verifies installation, not a live execution of every skill. Existing GBrain configuration is not pinned to this worktree; investigation used source search. No GBrain corpus upload was requested or used. Existing G-Stack telemetry setting is off; global preferences were otherwise preserved.

The office-hours helper prepared a complete reviewer prompt, a native subagent independently wrote/sealed a JSON verdict, and the helper checked/finalized it: one round, 9/10, no findings. Evidence is retained under `docs/plans/library-redesign-review/`. The review judges a draft, not product readiness.

The extra external Claude review was rejected by automatic approval review because it would export the plan/architecture notes without destination-specific consent. The user then chose **keep review local; skip Claude**. No outside-Claude result is claimed and no retry was made. Global `codex_reviews` preference remains unchanged. CEO/design/engineering reviews stay `issues_open` until the owner settles their draft decisions; installation and review initiation do not mean those gates passed.

Follow-up workflow after owner review: finalize design/engineering decisions, implement the smallest slice, run relevant behavioral/native/hardware checks, review the diff, and invoke ship only with publication in scope. Agent guidance is canonical in [AGENTS.md](../../AGENTS.md); the official team bootstrap remains in [CLAUDE.md](../../CLAUDE.md).
