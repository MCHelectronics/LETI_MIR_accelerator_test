# FM test signal from sig_gen (tone 1 kHz, deviation 75 kHz, +100 kHz)
# -> fm_demod -> the dominant audio tone must be 1 kHz.
file(REMOVE "${OUT}.cf32" "${OUT}.cf32.json" "${OUT}.wav")
execute_process(COMMAND "${GEN}" --type fm --out "${OUT}.cf32" --samples 500000 RESULT_VARIABLE r OUTPUT_QUIET)
execute_process(COMMAND "${DEMOD}" "${OUT}.cf32" "${OUT}.wav" --offset 100000 RESULT_VARIABLE r2 OUTPUT_QUIET)
execute_process(COMMAND "${PEAK}" "${OUT}.wav" OUTPUT_VARIABLE f OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT r EQUAL 0 OR NOT r2 EQUAL 0 OR f LESS 980 OR f GREATER 1020)
  message(FATAL_ERROR "Expected 1000 Hz audio, got '${f}'")
endif()
