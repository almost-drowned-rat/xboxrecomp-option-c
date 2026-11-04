cmake_minimum_required(VERSION 3.20)
project(movie_skip_test C)
# movie_skip.c alone, with a test clock and stand-ins for the two input functions it calls (no SDL, no runtime).
enable_testing()
add_executable(movie-skip-test movie_skip_test.c ../../src/input/movie_skip.c)
target_compile_definitions(movie-skip-test PRIVATE XBOX_MOVIE_SKIP_NO_SDL)
target_include_directories(movie-skip-test PRIVATE ../../src/input ../../src)
add_test(NAME movie-skip-double-y COMMAND movie-skip-test)
