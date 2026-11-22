# Loads every package the space separated wildcards in PACKAGES name, the way a game client does, and
# fails on one the loader refuses or cannot load.
separate_arguments(PACKAGES)
execute_process(
  COMMAND "${UCC}" Editor.LoadPackage ${PACKAGES} -game -INI=Smoke.ini -USERINI=SmokeUser.ini -nohomedir
  RESULT_VARIABLE Result OUTPUT_VARIABLE Out ERROR_VARIABLE Out)
if(NOT Result EQUAL 0)
  message(FATAL_ERROR "The sweep exited with ${Result}:\n${Out}")
endif()
string(REGEX MATCHALL "Loading [^\n]*" Loaded "${Out}")
if(NOT Loaded)
  message(FATAL_ERROR "The sweep found no package:\n${Out}")
endif()
string(REGEX MATCHALL "(Refused |Error loading )[^\n]*" Hits "${Out}")
if(Hits)
  list(JOIN Hits "\n" Hits)
  message(FATAL_ERROR "The loader turned packages down:\n${Hits}")
endif()
