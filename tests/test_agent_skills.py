"""Structural checks for the project-local Claude Code skills and agent instructions.

These tests cannot prove that a model *selects* a skill (see docs/AGENT_TOOLING.md for the
live discovery results); they guarantee the files Claude Code reads are well formed.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SKILLS_DIR = ROOT / ".claude" / "skills"
EXPECTED = {
    "reverse-engineering",
    "binary-analysis",
    "ghidra-analysis",
    "runtime-debugging",
    "graphics-research",
    "research-documentation",
}
NAME_RE = re.compile(r"^[a-z0-9]+(-[a-z0-9]+)*$")
# Claude Code truncates long descriptions in the skill listing; stay well below the limit.
MAX_DESCRIPTION = 700


def parse_frontmatter(path):
    text = path.read_text(encoding="utf-8")
    m = re.match(r"^---\n(.*?)\n---\n", text, re.S)
    if not m:
        raise AssertionError(f"{path}: missing YAML frontmatter")
    fields = {}
    for line in m.group(1).splitlines():
        key, sep, value = line.partition(":")
        if not sep or line.startswith(" "):
            raise AssertionError(f"{path}: unsupported frontmatter line {line!r}")
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] == '"':
            value = value[1:-1]
        fields[key.strip()] = value
    return fields, text[m.end():]


class SkillFiles(unittest.TestCase):
    def test_exactly_the_expected_skills_exist(self):
        found = {p.parent.name for p in SKILLS_DIR.glob("*/SKILL.md")}
        self.assertEqual(found, EXPECTED)

    def test_frontmatter_is_valid_and_distinct(self):
        names, descriptions = set(), set()
        for skill in sorted(EXPECTED):
            path = SKILLS_DIR / skill / "SKILL.md"
            fields, body = parse_frontmatter(path)
            self.assertEqual(fields["name"], skill, "name must match its directory")
            self.assertRegex(fields["name"], NAME_RE)
            self.assertLessEqual(len(fields["name"]), 64)
            self.assertIn("description", fields)
            desc = fields["description"]
            self.assertGreater(len(desc), 150, f"{skill}: description too vague")
            self.assertLessEqual(len(desc), MAX_DESCRIPTION)
            self.assertIn("Use when", desc, f"{skill}: description must name its triggers")
            names.add(fields["name"])
            descriptions.add(desc)
            self.assertGreater(len(body.strip().splitlines()), 15)
        self.assertEqual(len(names), len(EXPECTED))
        self.assertEqual(len(descriptions), len(EXPECTED))

    def test_automatic_invocation_is_not_disabled(self):
        for skill in EXPECTED:
            fields, _ = parse_frontmatter(SKILLS_DIR / skill / "SKILL.md")
            self.assertNotIn("disable-model-invocation", fields)
            self.assertNotIn("user-invocable", fields)

    def test_skill_cross_references_resolve(self):
        for skill in EXPECTED:
            body = (SKILLS_DIR / skill / "SKILL.md").read_text(encoding="utf-8")
            for ref in re.findall(r"docs/[A-Za-z_]+\.md", body):
                self.assertTrue((ROOT / ref).is_file(), f"{skill}: missing {ref}")

    def test_no_private_machine_paths(self):
        pattern = re.compile(r"[A-Za-z]:\\\\?Users|/Users/|C:/Users", re.I)
        files = list(SKILLS_DIR.glob("*/SKILL.md"))
        files += [ROOT / "CLAUDE.md", ROOT / "docs" / "AGENT_TOOLING.md", ROOT / "docs" / "FUNCTION_REGISTRY.md"]
        for path in files:
            self.assertIsNone(pattern.search(path.read_text(encoding="utf-8")), f"{path.name}: private path")


class Instructions(unittest.TestCase):
    def test_claude_md_imports_agents_md_and_stays_small(self):
        text = (ROOT / "CLAUDE.md").read_text(encoding="utf-8")
        self.assertIn("@AGENTS.md", text.splitlines())
        self.assertLess(len(text.splitlines()), 30)

    def test_agents_md_lists_every_skill(self):
        text = (ROOT / "AGENTS.md").read_text(encoding="utf-8")
        for skill in EXPECTED:
            self.assertIn(f".claude/skills/{skill}/SKILL.md", text)

    def test_index_links_resolve_and_include_new_docs(self):
        index = (ROOT / "docs" / "INDEX.md").read_text(encoding="utf-8")
        for target in re.findall(r"\]\(([^)#]+?)(?:#[^)]*)?\)", index):
            if target.startswith(("http", "../")):
                continue
            self.assertTrue((ROOT / "docs" / target).exists(), f"INDEX.md: broken link {target}")
        self.assertIn("AGENT_TOOLING.md", index)
        self.assertIn("FUNCTION_REGISTRY.md", index)

    def test_registry_rows_carry_tags_and_hashes(self):
        text = (ROOT / "docs" / "FUNCTION_REGISTRY.md").read_text(encoding="utf-8")
        for sha in ("f85765f1b36734165fc22122050ca39ca1b8873f9b2d430b50a8271cde5d5136",
                    "e9aff44931aa6c361fdcc49d1f5969d19777923c8f6c2c2e18e439bfd53c871d"):
            self.assertIn(sha, text)
        for line in text.splitlines():
            if re.match(r"\| 0x[0-9A-Fa-f]", line):
                self.assertRegex(line, r"CONFIRMED|INFERRED|HYPOTHESIS|OPEN", line)


if __name__ == "__main__":
    unittest.main()
