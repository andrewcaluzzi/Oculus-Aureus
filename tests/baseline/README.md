# Demo baselines

Per-tick `GE_DEMODIAG=1` logs (`ge: demo tick` lines only) from the UNMODIFIED build,
taken at commit 6be5920 with `tests/demo_diag.ps1` (GE_TICKHZ=60, GE_FRAMES=2400).

- `dam1.a.txt` (453 lines), `facility1.a.txt` (419 lines): reference logs.
- Two runs per demo were byte-identical, so the oracle is deterministic.
- Later tasks re-run `demo_diag.ps1 -Demo <name> -Out <file>` with the bot off and diff against `*.a.txt`.
- The script writes LF endings and no BOM (matches .gitattributes eol=lf), so
  `git diff --no-index --stat <a> <new>` printing nothing means bit-identical.
