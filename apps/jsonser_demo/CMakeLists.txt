add_executable(jsonser_demo main.cpp world.hpp generate.hpp)
target_link_libraries(jsonser_demo PRIVATE jsonser::jsonser)
target_compile_features(jsonser_demo PRIVATE cxx_std_20)  # the library is C++17; the demo uses defaulted ==
jsonser_warnings(jsonser_demo)
if(WIN32)
  target_link_libraries(jsonser_demo PRIVATE psapi)
endif()
