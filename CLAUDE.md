@AGENTS.md

# Claude Code routing

- Project skills in `.claude/skills/` are discovered by description. Apply the matching one on your own
  for ordinary project work; the user does not have to name it. Read the skill before the first step of the task.
- Reverse engineering: `reverse-engineering` first (it routes to `binary-analysis`, `ghidra-analysis`,
  `runtime-debugging`, `graphics-research`); `research-documentation` whenever a finding is made.
- The connected REA MCP (Ghidra) is the preferred code-analysis tool: `mcp__rea__*`, load schemas with ToolSearch.
  Policy and verified capabilities: `docs/AGENT_TOOLING.md`. If REA is unavailable, say so.
