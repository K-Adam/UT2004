# Writes the inis the server tests run on, fresh for each ctest run because the game rewrites the ini it runs on.
cmake_minimum_required(VERSION 3.26)
include(${SOURCE}/Tools/IniOverride.cmake)
ut_ini_write(Smoke.ini ${SOURCE}/System/Default.ini ${SOURCE}/DevIni/DevOverrides.ini ${SOURCE}/Tools/SmokeOverrides.ini)
ut_ini_write(SmokeUser.ini ${SOURCE}/System/DefUser.ini ${SOURCE}/DevIni/DevUserOverrides.ini)
