# Packs libmain.so, its shared libraries and the System and script files into build/android-arm64/UT2004.apk.
find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)

set(UT_JNILIBS ${CMAKE_BINARY_DIR}/jniLibs/${ANDROID_ABI})
if(CMAKE_HOST_WIN32)
  set(UT_GRADLE ${CMAKE_CURRENT_SOURCE_DIR}/gradlew.bat)
else()
  set(UT_GRADLE ${CMAKE_CURRENT_SOURCE_DIR}/gradlew)
endif()

add_custom_target(ut_apk
  COMMAND ${CMAKE_COMMAND} -E make_directory ${UT_JNILIBS}
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDLLaunch> $<TARGET_FILE:OpenAL::OpenAL>
          ${CMAKE_SYSROOT}/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/libc++_shared.so ${UT_JNILIBS}
  COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/pack_assets.py --out ${CMAKE_BINARY_DIR}/assets
          --source ${CMAKE_SOURCE_DIR} --scripts ${UT_SCRIPTS_DIR}
  COMMAND ${UT_GRADLE} --project-dir ${CMAKE_CURRENT_SOURCE_DIR} -PutBuild=${CMAKE_BINARY_DIR} -PutVersion=${UT_VERSION} assembleRelease
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/gradle/outputs/apk/release/app-release.apk ${CMAKE_BINARY_DIR}/UT2004.apk
  DEPENDS SDLLaunch
  USES_TERMINAL VERBATIM)
