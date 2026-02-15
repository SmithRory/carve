# AGENTS Guide

This file defines how agents should behave in this repository.

## Reference
- `README.md`

## Priority Rule
- Within each section, rules are listed from highest priority to lowest priority.

## Communication Rules
- Keep language neutral and factual.
- Do not use emotional wording.
- Do not present uncertain information as certain.

## Git Safety Rules
- Use only read-only git commands.
- Allowed examples: `git diff`, `git status`, `git log`, `git rev-parse`.
- Do not run git commands that change repository state.

## C++ Standards
- Use modern C++23.
- Prefer RAII over C-style memory management.
- Keep exception usage minimal.
- Follow the latest MISRA C++ guidelines.

## Code Change Rules
- Treat user removals as intentional: do not re-add code, config, docs, steps, or text that the user removed unless the user explicitly asks for that reintroduction in the current turn.
- Change only what the user asked to change.
- Keep edits minimal and focused.
- Do not add extra helper functions unless required for the requested change.
- Inspect surrounding code before making edits.

## High level goals
This project aims to replace some of the hammer source editors functionality. They key parts are:
1. Ease of use
2. Level conversion into a .vmf hammer file
3. Vertex based geometry
4. Cross platform natively (native linux, wsl, and windows)