# Runs a headless server on the development content for a while and fails on missing content.
execute_process(
  COMMAND "${UCC}" server DM-Rankin?game=XGame.xDeathMatch?NumBots=4 -INI=Smoke.ini -USERINI=SmokeUser.ini -nohomedir -multihome=127.0.0.1
  TIMEOUT 40 OUTPUT_VARIABLE Out ERROR_VARIABLE Out)
if(NOT Out MATCHES "Bringing Level DM-Rankin")
  message(FATAL_ERROR "The server did not start the level:\n${Out}")
endif()
if(Out MATCHES "Missing|Failed to load")
  message(FATAL_ERROR "The server reported missing content:\n${Out}")
endif()
