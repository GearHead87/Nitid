# Release tests

Tests of the release build on a machine with an NVIDIA RTX GPU. GitHub Actions runners have no GPU, so CI
checks only that the tarball builds, passes the unit tests and the dependency check, and starts on Ubuntu,
Debian, Fedora and Arch. These tests check that it really works. Run them before tagging a release
([docs/releasing.md](../../docs/releasing.md)). One folder per test, each with `run.sh`, `NOTES.md` and the
dated output.

| Test | Question | Result | Date | Notes |
|---|---|---|---|---|
| 01 | Linux tarball (built in Docker like CI): runs on this machine like a user would use it? | ✅ | 2026-11-26 | [01-linux-tarball](01-linux-tarball/NOTES.md): nitid-setup-vfx installs the runtime; 165 fps, 0 missed refreshes (540p, 1080p, 4K AV1); output identical to the developer build |
| 01 | 0.2.0 tarball: sound, seeking, FSR / NIS, on-screen controls | ✅ | 2026-11-26 | [01-linux-tarball](01-linux-tarball/NOTES.md#020-2026-11-26): A/V +2.3 ms, seek 46 ms, identical frames to the developer build (VSR + FG, FSR + seek) |
| 01 | 0.3.0 tarball: render-ahead (frame gen HIGH), compare view, clean-up | ✅ | 2026-11-26 | [01-linux-tarball](01-linux-tarball/NOTES.md#030-2026-11-26): HIGH at stride 2, 165 fps; A/V −0.7 ms; identical frames to the developer build |
