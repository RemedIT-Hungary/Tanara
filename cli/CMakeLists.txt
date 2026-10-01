add_executable(tanara-cli main.cpp CloudCommands.cpp CloudCommands.h)
target_link_libraries(tanara-cli PRIVATE tanara_core)
