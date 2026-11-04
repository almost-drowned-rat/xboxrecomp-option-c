cmake_minimum_required(VERSION 3.20)
project(apu_doorbell_test C)
set(CMAKE_C_STANDARD 11)
add_subdirectory(../.. runtime)
add_executable(apu_doorbell_test test_main.c)
target_include_directories(apu_doorbell_test PRIVATE ../../src ../../src/apu)
target_link_libraries(apu_doorbell_test PRIVATE xboxrecomp)
enable_testing()
# Two processes: dsp_ack_init caches RECOMP_APU_DSP_ACK in a static. The off
# arm is the negative control - the same posted command must stay unanswered.
add_test(NAME apu_doorbell_auto COMMAND apu_doorbell_test)
set_tests_properties(apu_doorbell_auto PROPERTIES
                     ENVIRONMENT "RECOMP_APU_DSP_ACK=auto;APU_DOORBELL_EXPECT=1")
add_test(NAME apu_doorbell_off COMMAND apu_doorbell_test)
set_tests_properties(apu_doorbell_off PROPERTIES
                     ENVIRONMENT "RECOMP_APU_DSP_ACK=;APU_DOORBELL_EXPECT=0")
