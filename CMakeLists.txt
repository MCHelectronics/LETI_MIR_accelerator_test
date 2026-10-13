cmake_minimum_required(VERSION 3.20)
project(QPSK_SDR_Lab LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)
option(LAB_WITH_IIO "Build programs that use local libiio and libad9361-iio" OFF)

add_executable(env_report examples/ch02/env_report.cpp)
add_executable(iq_tone examples/ch02/iq_tone.cpp)
foreach(target env_report iq_tone)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
endforeach()

if(BUILD_TESTING)
  add_test(NAME environment COMMAND env_report)
  add_test(NAME tone_file COMMAND ${CMAKE_COMMAND}
    -DPROGRAM=$<TARGET_FILE:iq_tone> -DOUT=${CMAKE_CURRENT_BINARY_DIR}/test-tone.cf32
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/tone_file.cmake)
  foreach(bad nan inf 1000000junk)
    add_test(NAME reject_rate_${bad} COMMAND iq_tone unused.cf32 ${bad} 10 0.01 0.5)
    set_tests_properties(reject_rate_${bad} PROPERTIES WILL_FAIL TRUE)
  endforeach()
  add_test(NAME reject_nyquist COMMAND iq_tone unused.cf32 1000 500 0.01 0.5)
  set_tests_properties(reject_nyquist PROPERTIES WILL_FAIL TRUE)
endif()

if(LAB_WITH_IIO)
  set(LAB_IIO_PREFIX "" CACHE PATH "Absolute local libiio installation directory")
  if(NOT IS_ABSOLUTE "${LAB_IIO_PREFIX}")
    message(FATAL_ERROR "Set -DLAB_IIO_PREFIX=/absolute/path/to/build/iio-install")
  endif()
  # Do not silently fall back to globally installed libiio or libad9361-iio.
  find_path(LAB_IIO_INCLUDE iio.h PATHS "${LAB_IIO_PREFIX}/include" NO_DEFAULT_PATH REQUIRED)
  find_library(LAB_IIO_LIBRARY NAMES iio PATHS "${LAB_IIO_PREFIX}/lib" NO_DEFAULT_PATH REQUIRED)
  find_path(LAB_AD9361_INCLUDE ad9361.h PATHS "${LAB_IIO_PREFIX}/include" NO_DEFAULT_PATH REQUIRED)
  find_library(LAB_AD9361_LIBRARY NAMES ad9361 PATHS "${LAB_IIO_PREFIX}/lib" NO_DEFAULT_PATH REQUIRED)
  # Common target for all programs that talk to the board.
  add_library(lab_iio INTERFACE)
  target_include_directories(lab_iio INTERFACE "${LAB_IIO_INCLUDE}" "${LAB_AD9361_INCLUDE}")
  target_link_libraries(lab_iio INTERFACE "${LAB_IIO_LIBRARY}" "${LAB_AD9361_LIBRARY}")
  add_executable(qpsk_channels examples/ch02/qpsk_channels.cpp)
  target_link_libraries(qpsk_channels PRIVATE lab_iio)
  target_compile_options(qpsk_channels PRIVATE -Wall -Wextra -Wpedantic)
  set_target_properties(qpsk_channels PROPERTIES BUILD_RPATH "${LAB_IIO_PREFIX}/lib")
  if(BUILD_TESTING)
    foreach(mode 2r2t 1r1t missing_tx)
      add_test(NAME channels_${mode} COMMAND qpsk_channels
        "xml:${CMAKE_CURRENT_SOURCE_DIR}/tests/${mode}.xml")
      set_tests_properties(channels_${mode} PROPERTIES
        ENVIRONMENT "LD_LIBRARY_PATH=${LAB_IIO_PREFIX}/lib")
    endforeach()
    set_tests_properties(channels_1r1t channels_missing_tx PROPERTIES WILL_FAIL TRUE)
  endif()
endif()

# Common course code: IQ files with JSON metadata (chapter 5 onwards).
if(LAB_WITH_IIO OR LAB_WITH_DSP)
  add_library(lab_core STATIC src/iq_file.cpp)
  target_include_directories(lab_core PUBLIC include)
  target_compile_options(lab_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(LAB_WITH_IIO)
  # Chapter 3: board state and RX throughput. Their results belong to a
  # particular board and link, so they are checked on the stand, not by CTest.
  foreach(target board_state rx_benchmark)
    add_executable(${target} examples/ch03/${target}.cpp)
    target_link_libraries(${target} PRIVATE lab_iio)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    set_target_properties(${target} PROPERTIES BUILD_RPATH "${LAB_IIO_PREFIX}/lib")
  endforeach()
  add_executable(rx_capture examples/ch05/rx_capture.cpp)
  target_link_libraries(rx_capture PRIVATE lab_iio lab_core)
  target_compile_options(rx_capture PRIVATE -Wall -Wextra -Wpedantic)
  set_target_properties(rx_capture PROPERTIES BUILD_RPATH "${LAB_IIO_PREFIX}/lib")
endif()

option(LAB_WITH_DSP "Build chapter 5 programs that use FFTW3 and OpenCV" OFF)
if(LAB_WITH_DSP)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(FFTW3F REQUIRED IMPORTED_TARGET fftw3f)
  find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)
  add_library(lab_dsp STATIC src/dsp.cpp src/plot.cpp)
  target_include_directories(lab_dsp PUBLIC ${OpenCV_INCLUDE_DIRS})
  target_link_libraries(lab_dsp PUBLIC lab_core PkgConfig::FFTW3F ${OpenCV_LIBS})
  target_compile_options(lab_dsp PRIVATE -Wall -Wextra -Wpedantic)
  foreach(target spectrum_view fm_demod)
    add_executable(${target} examples/ch05/${target}.cpp)
    target_link_libraries(${target} PRIVATE lab_dsp)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endforeach()
  if(BUILD_TESTING)
    add_executable(gen_fm tests/gen_fm.cpp)
    target_link_libraries(gen_fm PRIVATE lab_core)
    add_executable(wav_peak tests/wav_peak.cpp)
    target_link_libraries(wav_peak PRIVATE lab_dsp)
    add_test(NAME spectrum_tone COMMAND ${CMAKE_COMMAND}
      -DTONE=$<TARGET_FILE:iq_tone> -DVIEW=$<TARGET_FILE:spectrum_view>
      -DOUT=${CMAKE_CURRENT_BINARY_DIR}/test-spectrum
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/ch05_spectrum.cmake)
    add_test(NAME fm_demod_tone COMMAND ${CMAKE_COMMAND}
      -DGEN=$<TARGET_FILE:gen_fm> -DDEMOD=$<TARGET_FILE:fm_demod> -DPEAK=$<TARGET_FILE:wav_peak>
      -DOUT=${CMAKE_CURRENT_BINARY_DIR}/test-fm
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/ch05_fm.cmake)
    add_test(NAME spectrum_rejects_missing_file COMMAND spectrum_view missing.cf32)
    set_tests_properties(spectrum_rejects_missing_file PROPERTIES WILL_FAIL TRUE)
  endif()
endif()

# Chapter 6: test signals and transmission. Needs the IIO and DSP parts.
option(LAB_WITH_TX "Build chapter 6 programs: signal generator and TX player" OFF)
if(LAB_WITH_TX)
  if(NOT LAB_WITH_IIO OR NOT LAB_WITH_DSP)
    message(FATAL_ERROR "LAB_WITH_TX requires LAB_WITH_IIO=ON and LAB_WITH_DSP=ON")
  endif()
  add_executable(sig_gen examples/ch06/sig_gen.cpp)
  target_link_libraries(sig_gen PRIVATE lab_core)
  add_executable(tx_play examples/ch06/tx_play.cpp)
  target_link_libraries(tx_play PRIVATE lab_iio lab_core)
  set_target_properties(tx_play PROPERTIES BUILD_RPATH "${LAB_IIO_PREFIX}/lib")
  foreach(target sig_gen tx_play)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endforeach()
  if(BUILD_TESTING)
    add_test(NAME am_sidebands COMMAND ${CMAKE_COMMAND}
      -DGEN=$<TARGET_FILE:sig_gen> -DVIEW=$<TARGET_FILE:spectrum_view>
      -DOUT=${CMAKE_CURRENT_BINARY_DIR}/test-am
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/ch06_am.cmake)
    add_test(NAME fm_loop COMMAND ${CMAKE_COMMAND}
      -DGEN=$<TARGET_FILE:sig_gen> -DDEMOD=$<TARGET_FILE:fm_demod> -DPEAK=$<TARGET_FILE:wav_peak>
      -DOUT=${CMAKE_CURRENT_BINARY_DIR}/test-fm6
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/ch06_fm.cmake)
    # Without --go the player must refuse before opening the board.
    add_test(NAME tx_requires_go COMMAND tx_play --uri ip:192.0.2.1 --freq 433.92e6
      --in missing.cf32 --tx-gain -60)
    set_tests_properties(tx_requires_go PROPERTIES WILL_FAIL TRUE)
  endif()
endif()
