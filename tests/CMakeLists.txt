find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(tst_transport tst_transport.cpp)
target_link_libraries(tst_transport PRIVATE pot-player-core Qt6::Test)
add_test(NAME tst_transport COMMAND tst_transport)

add_executable(tst_emptystate tst_emptystate.cpp)
target_link_libraries(tst_emptystate PRIVATE pot-player-core Qt6::Test)
add_test(NAME tst_emptystate COMMAND tst_emptystate)
