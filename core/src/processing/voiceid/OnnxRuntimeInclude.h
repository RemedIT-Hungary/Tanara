#pragma once
//
// Private include of the ONNX Runtime C++ API for the processing module's ORT users (VoiceEmbedder,
// the pyannote segmentation model). Only compiled in voice-ID builds (TANARA_HAVE_VOICEID).
//
// --- SAL shim for MinGW --------------------------------------------------------
// On Windows the ONNX Runtime C-API header relies on <specstrings.h> for the SAL2 annotations
// (_In_, _Outptr_, _Frees_ptr_opt_, ...). MinGW's specstrings.h is old and lacks several SAL2
// macros, so undefined tokens make function-pointer parameters "disappear" (implicit int -> a
// compile error). Solution: on MinGW (only there, NOT on MSVC) we define as empty, BEFORE the include,
// exactly the set the header itself uses in its non-Windows branch. The switch is a compile definition
// (TANARA_ORT_SAL_SHIM, set by core/CMakeLists.txt for MinGW), not a platform macro: it is about the
// compiler, not the OS.
// (#ifndef guard: the definitions already in specstrings.h do not collide.)
#ifdef TANARA_ORT_SAL_SHIM
#  ifndef _In_
#    define _In_
#  endif
#  ifndef _In_z_
#    define _In_z_
#  endif
#  ifndef _In_opt_
#    define _In_opt_
#  endif
#  ifndef _In_opt_z_
#    define _In_opt_z_
#  endif
#  ifndef _Out_
#    define _Out_
#  endif
#  ifndef _Outptr_
#    define _Outptr_
#  endif
#  ifndef _Out_opt_
#    define _Out_opt_
#  endif
#  ifndef _Inout_
#    define _Inout_
#  endif
#  ifndef _Inout_opt_
#    define _Inout_opt_
#  endif
#  ifndef _Frees_ptr_opt_
#    define _Frees_ptr_opt_
#  endif
#  ifndef _Ret_maybenull_
#    define _Ret_maybenull_
#  endif
#  ifndef _Ret_notnull_
#    define _Ret_notnull_
#  endif
#  ifndef _Check_return_
#    define _Check_return_
#  endif
#  ifndef _Outptr_result_maybenull_
#    define _Outptr_result_maybenull_
#  endif
#  ifndef _In_reads_
#    define _In_reads_(X)
#  endif
#  ifndef _Inout_updates_
#    define _Inout_updates_(X)
#  endif
#  ifndef _Out_writes_
#    define _Out_writes_(X)
#  endif
#  ifndef _Inout_updates_all_
#    define _Inout_updates_all_(X)
#  endif
#  ifndef _Out_writes_bytes_all_
#    define _Out_writes_bytes_all_(X)
#  endif
#  ifndef _Out_writes_all_
#    define _Out_writes_all_(X)
#  endif
#  ifndef _Success_
#    define _Success_(X)
#  endif
#  ifndef _Outptr_result_buffer_maybenull_
#    define _Outptr_result_buffer_maybenull_(X)
#  endif
#endif  // TANARA_ORT_SAL_SHIM

#include <onnxruntime_cxx_api.h>

#include <QString>

#include <string>
#include <type_traits>

namespace tanara::processing {

// A model path as ONNX Runtime wants it: ORTCHAR_T is wchar_t on Windows (wide path), char elsewhere.
template <class C = ORTCHAR_T>
std::basic_string<C> ortPath(const QString& path)
{
    if constexpr (std::is_same_v<C, wchar_t>)
        return path.toStdWString();
    else
        return path.toUtf8().toStdString();
}

} // namespace tanara::processing
