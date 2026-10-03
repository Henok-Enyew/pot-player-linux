find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(tst_transport tst_transport.cpp)
target_link_libraries(tst_transport PRIVATE top-player-core Qt6::Test)
add_test(NAME tst_transport COMMAND tst_transport)

add_executable(tst_emptystate tst_emptystate.cpp)
target_link_libraries(tst_emptystate PRIVATE top-player-core Qt6::Test)
add_test(NAME tst_emptystate COMMAND tst_emptystate)

add_executable(tst_playlist tst_playlist.cpp)
target_link_libraries(tst_playlist PRIVATE top-player-core Qt6::Test)
add_test(NAME tst_playlist COMMAND tst_playlist)

add_executable(tst_audio tst_audio.cpp)
target_link_libraries(tst_audio PRIVATE top-player-core Qt6::Test)
add_test(NAME tst_audio COMMAND tst_audio)

add_executable(tst_subtitles tst_subtitles.cpp)
target_link_libraries(tst_subtitles PRIVATE top-player-core Qt6::Test)
add_test(NAME tst_subtitles COMMAND tst_subtitles)
