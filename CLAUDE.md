# Pocket reader development

Read [AGENTS.md](AGENTS.md) for the shared project overview, architecture, development conventions, test requirements, rev2 hardware constraints and Git workflow. It is the canonical project guidance for both Claude and Codex.

This initialization branch contains environment setup and planning only. The next step is user review of `docs/plans/library-redesign.md`, not feature implementation. Never flash, merge or release during initialization.

## gstack (REQUIRED — global install)

**Before doing ANY work, verify gstack is installed:**

```bash
_GS=""
for _D in "${GSTACK_ROOT:-}" "$HOME/.claude/skills/gstack" "$HOME/.codex/skills/gstack" "$HOME/.factory/skills/gstack" "$HOME/.kiro/skills/gstack" "$HOME/.config/opencode/skills/gstack" "$HOME/.slate/skills/gstack" "$HOME/.cursor/skills/gstack" "$HOME/.openclaw/skills/gstack" "$HOME/.hermes/skills/gstack" "$HOME/.gbrain/skills/gstack" "$HOME/.copilot/skills/gstack" "$HOME/.gemini/antigravity-cli/skills/gstack" "$HOME/.gstack/repos/gstack"; do
  [ -z "$_GS" ] && [ -n "$_D" ] && [ -d "$_D/bin" ] && _GS="$_D"
done
[ -n "$_GS" ] && echo "GSTACK_OK: $_GS" || echo "GSTACK_MISSING"
```

If GSTACK_MISSING: STOP. Do not proceed. Tell the user:

> gstack is required for all AI-assisted work in this repo.
> Install it:
> ```bash
> git clone --depth 1 https://github.com/garrytan/gstack.git ~/.claude/skills/gstack
> cd ~/.claude/skills/gstack && ./setup --team
> ```
> Then restart your AI coding tool.

Do not skip skills, ignore gstack errors, or work around missing gstack.

Using gstack skills: After install, skills like /qa, /ship, /review, /investigate,
and /browse are available. Use /browse for all web browsing (Aside first, the bundled gstack browser as fallback).
Use the resolved install path above for gstack file paths
(default: ~/.claude/skills/gstack).


## Skill routing

Use the official installed skills for their relevant tasks: office-hours for discovery; plan-ceo-review for scope; plan-design-review for design; plan-eng-review for architecture; investigate for root causes; review for code review; qa/qa-only for supported behavioral testing; ship only when publishing is explicitly in scope. Route only to skills verified in the current session. Native device tests remain necessary even when browser QA passes.

Codex: run the source checkout's `./setup --host codex`, verify `./setup --status`, then start a fresh agent session. Skills are named `gstack-office-hours`, `gstack-plan-ceo-review`, etc. The official Claude team hook is not a Codex enforcement hook. See `docs/development/gstack.md` for verified installation and limitations.
