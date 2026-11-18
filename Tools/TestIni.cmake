# Writes the inis the server tests run on, fresh for each ctest run because the game rewrites the ini it runs on.
# The uplinks are cut so a test never advertises to the public masters.
file(READ ${INI_DIR}/Dev.ini Ini)
string(REGEX REPLACE "MasterServerList=[^\n]*\n" "" Ini "${Ini}")
string(REGEX REPLACE "DoUplink=True" "DoUplink=False" Ini "${Ini}")
string(REGEX REPLACE "UplinkToGamespy=True" "UplinkToGamespy=False" Ini "${Ini}")
string(REGEX REPLACE "WebMasterServer=[^\r\n]*" "WebMasterServer=" Ini "${Ini}")
file(WRITE Smoke.ini "${Ini}")
file(COPY_FILE ${INI_DIR}/DevUser.ini SmokeUser.ini)
