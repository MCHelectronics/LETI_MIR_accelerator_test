# AM, mu = 0.5, A = 0.5, carrier +100 kHz, tone 10 kHz; N = 1000 puts every
# line on an FFT bin. Expected: carrier 20*log10(0.5) = -6.02 dBFS,
# sidebands 20*log10(0.5*0.5/2) = -18.06 dBFS at 90 and 110 kHz.
file(REMOVE "${OUT}.cf32" "${OUT}.cf32.json")
execute_process(COMMAND "${GEN}" --type am --out "${OUT}.cf32" --mod-freq 10e3 RESULT_VARIABLE r OUTPUT_QUIET)
execute_process(COMMAND "${VIEW}" "${OUT}.cf32" --nfft 1000 --peaks 3 RESULT_VARIABLE r2 OUTPUT_VARIABLE out)
message(STATUS "${out}")
if(NOT r EQUAL 0 OR NOT r2 EQUAL 0)
  message(FATAL_ERROR "sig_gen or spectrum_view failed")
endif()
# Levels are printed with two decimals: compare them in hundredths of dB.
foreach(pair "100000.0:602" "90000.0:1806" "110000.0:1806")
  string(REPLACE ":" ";" pair "${pair}")
  list(GET pair 0 freq)
  list(GET pair 1 expect)
  if(NOT out MATCHES "\\+ *${freq} Hz +-([0-9]+)\\.([0-9][0-9]) dBFS")
    message(FATAL_ERROR "Line at +${freq} Hz not found")
  endif()
  math(EXPR delta "${CMAKE_MATCH_1}${CMAKE_MATCH_2} - ${expect}")
  if(delta LESS -30 OR delta GREATER 30)
    message(FATAL_ERROR "Line +${freq} Hz: -${CMAKE_MATCH_1}.${CMAKE_MATCH_2} dBFS, expected -${expect}/100")
  endif()
endforeach()
