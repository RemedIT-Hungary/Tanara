# OnnxRuntimeWin.cmake — a hivatalos ONNX Runtime win-x64 release bedrótozása.
#
# Használat: a top-level CMakeLists hívja, ha WIN32 ÉS ONNXRUNTIME_ROOT_DIR meg van adva:
#   cmake ... -DTANARA_BUILD_VOICEID=ON -DONNXRUNTIME_ROOT_DIR=C:/.../onnxruntime-win-x64-1.20.1
#
# A release-mappa szerkezete:  include/  lib/onnxruntime.dll  lib/onnxruntime.lib
# Létrehoz egy onnxruntime::onnxruntime importált SHARED targetet (ezt linkeli a core).
# A futtatáshoz az onnxruntime.dll-t a tanara.exe mellé kell tenni (a windeployqt
# ezt nem húzza be — a packaging lépés másolja).

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

# A futásidejű DLL elérési útja a packaginghez (a tanara.exe mellé kell másolni).
set(ONNXRUNTIME_DLL "${_ort_dll}" CACHE FILEPATH "ONNX Runtime futásidejű DLL" FORCE)
message(STATUS "ONNX Runtime (win-x64): ${ONNXRUNTIME_ROOT_DIR}")
