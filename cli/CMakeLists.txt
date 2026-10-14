add_executable(tanara-cli main.cpp CloudCommands.cpp CloudCommands.h VoiceCommands.cpp VoiceCommands.h
    TrackSidesCommand.cpp TrackSidesCommand.h TagPeopleCommands.cpp TagPeopleCommands.h
    ParticipantsCommand.cpp ParticipantsCommand.h ReviewCommand.cpp ReviewCommand.h
    ResliceCommand.cpp ResliceCommand.h PathsCommands.cpp PathsCommands.h
    MigrateCommands.cpp MigrateCommands.h legacy/LegacyPeople.cpp legacy/LegacyPeople.h)
target_link_libraries(tanara-cli PRIVATE tanara_core)
tanara_copy_onnxruntime_dll(tanara-cli)
