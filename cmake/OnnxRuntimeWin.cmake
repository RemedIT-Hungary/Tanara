# OnnxRuntimeWin.cmake: wires up the official ONNX Runtime win-x64 release.
#
# Usage: called from the top-level CMakeLists when WIN32 and ONNXRUNTIME_ROOT_DIR are set:
#   cmake ... -DTANARA_BUILD_VOICEID=ON -DONNXRUNTIME_ROOT_DIR=C:/.../onnxruntime-win-x64-1.20.1
#
# Release folder layout:  include/  lib/onnxruntime.dll  lib/onnxruntime.lib
# Creates an imported SHARED target onnxruntime::onnxruntime (linked by core).
# At run time onnxruntime.dll must sit next to tanara.exe (windeployqt does not pull it
# in; the packaging step copies it).

if(NOT EXISTS "${ONNXRUNTIME_ROOT_DIR}/include/onnxruntime_cxx_api.h")
    message(FATAL_ERROR
        "ONNXRUNTIME_ROOT_DIR (${ONNXRUNTIME_ROOT_DIR}) nem tűnik érvényes ONNX Runtime "
        "release-nek (hiányzik az include/onnxruntime_cxx_api.h).")
endif()

set(_ort_dll "${ONNXRUNTIME_ROOT_DIR}/lib/onnxruntime.dll")
set(_ort_lib "${ONNXRUNTIME_ROOT_DIR}/lib/onnxruntime.lib")

add_library(onnxruntime::onnxruntime SHARED IMPORTED GLOBAL)
set_target_properties(onnxruntime::onnxruntime PROPERTIES
    IMPORTED_LOCATION "${_ort_dll}"
    IMPORTED_IMPLIB   "${_ort_lib}"
    INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_ROOT_DIR}/include")

# Runtime DLL path for packaging (must be copied next to tanara.exe).
set(ONNXRUNTIME_DLL "${_ort_dll}" CACHE FILEPATH "ONNX Runtime futásidejű DLL" FORCE)
message(STATUS "ONNX Runtime (win-x64): ${ONNXRUNTIME_ROOT_DIR}")
