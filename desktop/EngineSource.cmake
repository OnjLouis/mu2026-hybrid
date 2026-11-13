# Do not change the pinned checkout or shared S-MU2000 settings. Default this
# frontend to one DSP thread, retaining upstream's explicit threaded override.
file(READ "${SMU_SOURCE}/src/vst3/engine.cpp" pilot_engine)
set(engine_default "bool threaded = true;")
string(FIND "${pilot_engine}" "${engine_default}" engine_default_at)
if(engine_default_at EQUAL -1)
  message(FATAL_ERROR "Pinned S-MU2000 threading default changed; review the pilot adapter")
endif()
string(REPLACE "${engine_default}" "bool threaded = false;" pilot_engine "${pilot_engine}")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/PilotEngine.cpp" CONTENT "${pilot_engine}" @ONLY)
