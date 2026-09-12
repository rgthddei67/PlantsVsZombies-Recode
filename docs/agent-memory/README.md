# Codex Project Memory

[项目文档导航](../README.md) · [子系统历史索引](MEMORY.md)

> 本目录保存历史工程上下文。当前使用方法从项目指南进入；旧方案的状态解释见 [设计与计划阅读说明](../superpowers/README.md)。

This directory is the durable project-memory store for Codex work on `PlantVsZombies`.

It was migrated on 2026-07-17 from the Claude Code project key `D--PVZ-PlantsVsZombies-PlantVsZombies`. The migration copied all 66 source Markdown files (288,537 bytes), verified every initial copy by SHA-256, and then converted 165 Claude-style aliases and wiki links into portable relative Markdown links. No engineering prose or metadata was intentionally rewritten. The original hashes are recorded in `SOURCE_SHA256SUMS.txt`.

`MEMORY.md` is the routing index; the remaining migrated Markdown files are focused project, feedback, or reference memories.

## Usage contract

1. For current behavior or small changes, start with source/configuration. Search `MEMORY.md` only for needed historical reasons, exceptions or missing entrypoints.
2. Read only the linked topic sections needed for the current task.
3. Verify only historical claims the current conclusion relies on; old instructions do not override current AGENTS.md or the user.
4. Maintain only reusable knowledge actually affected by the task, following [AGENTS.md](../../AGENTS.md#文档与记忆); routine tuning does not require memory updates.
5. Keep durable mandatory rules in AGENTS.md and conditional technical detail in skill references. Do not copy current tuning tables or per-run logs here.

The old `~/.claude/projects/.../memory/` directory may remain as a backup, but it is no longer the authoritative copy.
