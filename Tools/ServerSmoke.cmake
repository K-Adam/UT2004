# Runs a headless server on the development content for a while and fails on missing content.
# The uplinks are cut so a test never advertises to the public masters.
file(READ ${INI_DIR}/Dev.ini Ini)
string(REGEX REPLACE "MasterServerList=[^\n]*\n" "" Ini "${Ini}")
string(REGEX REPLACE "DoUplink=True" "DoUplink=False" Ini "${Ini}")
string(REGEX REPLACE "UplinkToGamespy=True" "UplinkToGamespy=False" Ini "${Ini}")
string(REGEX REPLACE "WebMasterServer=[^\r\n]*" "WebMasterServer=" Ini "${Ini}")
file(WRITE Smoke.ini "${Ini}")
file(COPY_FILE ${INI_DIR}/DevUser.ini SmokeUser.ini)
execute_process(
  COMMAND "${UCC}" server DM-Rankin?game=XGame.xDeathMatch?NumBots=4 -INI=Smoke.ini -USERINI=SmokeUser.ini -nohomedir
  TIMEOUT 40 OUTPUT_VARIABLE Out ERROR_VARIABLE Out)
if(NOT Out MATCHES "Bringing Level DM-Rankin")
  message(FATAL_ERROR "The server did not start the level:\n${Out}")
endif()
if(Out MATCHES "Missing|Failed to load")
  message(FATAL_ERROR "The server reported missing content:\n${Out}")
endif()
