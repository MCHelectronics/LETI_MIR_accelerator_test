# Synthetic FM (1 kHz tone at +200 kHz) -> fm_demod -> dominant audio tone 1 kHz.
file(REMOVE "${OUT}.cf32" "${OUT}.cf32.json" "${OUT}.wav")
execute_process(COMMAND "${GEN}" "${OUT}.cf32" RESULT_VARIABLE r)
execute_process(COMMAND "${DEMOD}" "${OUT}.cf32" "${OUT}.wav" --offset 200000 RESULT_VARIABLE r2 OUTPUT_VARIABLE log)
message(STATUS "${log}")
execute_process(COMMAND "${PEAK}" "${OUT}.wav" OUTPUT_VARIABLE f OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT r EQUAL 0 OR NOT r2 EQUAL 0 OR f LESS 980 OR f GREATER 1020)
  message(FATAL_ERROR "Expected 1000 Hz audio, got '${f}'")
endif()
