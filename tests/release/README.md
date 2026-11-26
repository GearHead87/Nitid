# Release tests

Tests of the release build on a machine with an NVIDIA RTX GPU. GitHub Actions runners have no GPU, so CI
checks only that the tarball builds, passes the unit tests and the dependency check, and starts on Ubuntu,
Debian, Fedora and Arch. These tests check that it really works. Run them before tagging a release
([docs/releasing.md](../../docs/releasing.md)). One folder per test, each with `run.sh`, `NOTES.md` and the
dated output.

| Test | Question | Result | Date | Notes |
|---|---|---|---|---|
| 01 | Linux tarball (built in Docker like CI): runs on this machine like a user would use it? | ✅ | 2026-11-26 | [01-linux-tarball](01-linux-tarball/NOTES.md): nitid-setup-vfx installs the runtime; 165 fps, 0 missed refreshes (540p, 1080p, 4K AV1); output identical to the developer build |
