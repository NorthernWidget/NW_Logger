# NW_Logger

The logger core that Margay_Library and Okapi_Library share, extracted from Margay_Library on 2026-09-23 (design: NW-Device-Specification/LIBRARY-DESIGN.md section 12). `NW_Logger` is the base class; a board's library inherits it and fills in the hooks.

## Working rules

- Extracted, never designed ahead: a function moves here only when Margay and Okapi have the same body. Margay's version wins where they differ, and the reason is in the commit.
- Acceptance for every change: all NW-Tests sketches compile (`python3 NW-Tests/compile.py`), flash and RAM recorded in the commit, Margay's public surface unchanged.
- Formatting: the Arduino IDE auto-format (2-space indent, attached braces); moved code keeps the form it came with. `python3 NW-Tests/style_check.py .` gates every commit.
- Unversioned (0.0.0) and unregistered until the 2026 overhaul ends.

## Hard rule

**Never** create a git tag, GitHub release, push to a shared remote, or submit to any external registry unless explicitly asked in the current message. If in doubt, ask.
