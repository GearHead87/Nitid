# Releasing Nitid

Releases are Linux x86_64 tarballs built by GitHub Actions and published as GitHub releases with a changelog.
The same build runs locally in Docker, so a release is never the first time the build runs.

## Making a release

```sh
scripts/bump-version.sh 0.2.0          # sets the version in the 4 CMakeLists.txt, starts CHANGELOG's 0.2.0 section
$EDITOR CHANGELOG.md                   # write the 0.2.0 section (Added / Changed / Fixed / Known limits)
packaging/linux/docker-build.sh        # optional: build + test the tarball locally (out/)
git commit -am "Release 0.2.0" && git push
git tag -a v0.2.0 -m "Nitid 0.2.0" && git push origin v0.2.0
```

Pushing the tag runs [release.yml](../.github/workflows/release.yml). A tag with a suffix (`v0.2.0-rc.1`) makes
a pre-release that is not marked latest. Before tagging, "Run workflow" on the Release workflow without a tag
does a dry run: it builds and shows the release notes in the run summary, and publishes nothing.

Before a release, run the GPU test on a machine with an NVIDIA RTX GPU (CI runners have none):
[tests/release/01-linux-tarball/run.sh](../tests/release/01-linux-tarball/run.sh).

## What the release workflow does

| Job | What it does |
|---|---|
| Preflight | Tag format; tag matches the CMake versions and CHANGELOG has its section ([check-version.sh](../scripts/check-version.sh)); tag is on `main`; finds the previous release (a normal release skips release candidates) |
| Build | [build-linux.yml](../.github/workflows/build-linux.yml): builds the tarball on Ubuntu 24.04, unit tests, dependency check, starts it on Ubuntu 24.04, Debian 13, Fedora and Arch |
| Release notes | [release-notes.sh](../scripts/release-notes.sh): the version's CHANGELOG section, install steps, every commit since the previous release (folded), compare link |
| Publish | `SHA256SUMS`, build provenance attestation, draft release with the files, then published (only for tags) |

[ci.yml](../.github/workflows/ci.yml) runs the same build on every push to `main` and on pull requests.

## The tarball

Built by [packaging/linux/build-release.sh](../packaging/linux/build-release.sh):

```
nitid-X.Y.Z-linux-x86_64/
  bin/nitid, bin/nitid-cli      programs (RPATH $ORIGIN/../lib)
  bin/nitid-setup-vfx           installs NVIDIA's VFX runtime for this user
  bin/nitid-setup-dlss          installs NVIDIA's DLSS runtime for this user
  lib/libnitid.so.0             engine
  lib/libnitid-dlss.so          DLSS module (NVIDIA's DLSS SDK code; loaded at run time)
  lib/libav*.so, libsw*.so      FFmpeg 9.1, LGPL, NVDEC on, decoding only (packaging/linux/build-deps.sh)
  lib/libSDL3.so.0              SDL 3
  README.txt
  share/doc/nitid/              CHANGELOG.md, README.md, THIRD-PARTY-NOTICES.md, licenses/
```

- **Bundled:** FFmpeg and SDL3, built from source, because distros ship different FFmpeg versions. Versions and
  SHA-256 are pinned in [packaging/linux/versions.env](../packaging/linux/versions.env).
- **From the host** (checked by [check-deps.sh](../packaging/linux/check-deps.sh)): glibc 2.38+, libstdc++
  (GCC 13+), zlib, the Vulkan loader, and the NVIDIA driver (`libcuda.so.1`).
- **Not shipped: the NVIDIA VFX runtime** (NVIDIA's proprietary license, 580 MB). `nitid-setup-vfx` downloads
  NVIDIA's `nvidia-vfx` wheel from pypi.nvidia.com, checks its pinned SHA-256, and unpacks only the libraries
  into `~/.local/share/nitid/vfx` (keeping the wheel's layout, which its libraries' RPATH expects). libnitid
  looks there automatically (search order: `$NITID_VFX_DIR`, `$XDG_DATA_HOME/nitid/vfx/nvvfx/libs`,
  `<libnitid dir>/nitid/vfx/nvvfx/libs`, the developer path from the build).
- **Not shipped: the NVIDIA DLSS runtime** (`libnvidia-ngx-dlss.so`, `-dlssg.so` 310.9.16, 67 MB).
  `nitid-setup-dlss` downloads them from NVIDIA's DLSS SDK repository on GitHub (pinned tag, SHA-256 checked) into
  `~/.local/share/nitid/dlss`. The DLSS module itself is shipped: it contains NVIDIA's small NGX loader
  (`libnvsdk_ngx.a`), which the NVIDIA RTX SDKs license allows inside an application (license in
  `share/doc/nitid/licenses/DLSS-SDK`). It is a separate file because that license must not be combined with an
  open-source license.
- **Build:** CUDA 12.9 `nvcc` for the PTX kernel (only nvcc and the driver API headers + link stub are
  installed), GCC 14, the NVIDIA VFX SDK C headers from a pinned Maxine-VFX-SDK commit.

## Building locally (Docker)

```sh
packaging/linux/docker-build.sh [output dir]     # default out/
```

- Uses the image `nitid-build:ubuntu-24.04` ([Dockerfile](../packaging/linux/Dockerfile), the same
  [install-build-deps.sh](../packaging/linux/install-build-deps.sh) as CI, about 1.9 GB). It is kept between
  releases and rebuilt only when that script changes.
- Sources are mounted read-only. The build (FFmpeg/SDL3, objects) lives in the Docker volume
  `nitid-release-work`, so later builds take about a minute. Nothing large is written into the repo, only
  the tarball in `out/` (git-ignored).
- To free the space: `docker volume rm nitid-release-work` (the next build redoes FFmpeg/SDL3, about 5 minutes).

## Updating a pinned version

- FFmpeg / SDL3 / CLI11 / nv-codec-headers: change the version and SHA-256 in `versions.env`. The deps cache
  key changes with it.
- DLSS SDK: `DLSS_SDK_VERSION`, `DLSS_SDK_COMMIT` and `DLSS_NGX_LIB_SHA256` in `versions.env`, and the runtime
  files and hashes in [nitid-setup-dlss](../packaging/linux/nitid-setup-dlss).
- VFX runtime: change `VFX_VERSION`, `WHEEL` and `SHA256` in
  [nitid-setup-vfx](../packaging/linux/nitid-setup-vfx). The hash is on `https://pypi.nvidia.com/nvidia-vfx/`.
- CUDA: `CUDA_VERSION` in install-build-deps.sh.
