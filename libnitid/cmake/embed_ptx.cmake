# Writes a C header with the PTX text: cmake -DPTX=in.ptx -DOUT=out.h -DNAME=symbol -P embed_ptx.cmake
file(READ "${PTX}" ptx)
file(WRITE "${OUT}" "// Generated from ${PTX}. Do not edit.\n#pragma once\nstatic const char ${NAME}[] = R\"NITIDPTX(${ptx})NITIDPTX\";\n")
