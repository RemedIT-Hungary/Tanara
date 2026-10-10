add_executable(tanara-cli main.cpp CloudCommands.cpp CloudCommands.h VoiceCommands.cpp VoiceCommands.h
    TrackSidesCommand.cpp TrackSidesCommand.h)
target_link_libraries(tanara-cli PRIVATE tanara_core)
tanara_copy_onnxruntime_dll(tanara-cli)
