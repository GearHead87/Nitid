# Third-party code inside libnitid

| Folder | What | License | Used by |
|---|---|---|---|
| `nis/NIS_Config.h` | NVIDIA Image Scaling SDK v1.0.3 configuration and filter tables, unchanged ([source](https://github.com/NVIDIAGameWorks/NVIDIAImageScaling)) | MIT (`nis/LICENSE.txt`) | `src/gpu/spatial_scaler.cpp`; the NIS shader itself is ported to CUDA in `src/gpu/kernels/scale.cu` |
| `fsr1/` | License of AMD FidelityFX FSR 1 ([source](https://github.com/GPUOpen-Effects/FidelityFX-FSR)); EASU and RCAS are ported to CUDA in `src/gpu/kernels/scale.cu` | MIT (`fsr1/LICENSE.txt`) | `src/gpu/kernels/scale.cu` |
