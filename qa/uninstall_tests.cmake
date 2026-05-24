cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED SOURCE_DIR OR NOT DEFINED TEST_ROOT)
  message(FATAL_ERROR "SOURCE_DIR and TEST_ROOT are required")
endif()

function(assert_removed path)
  if(EXISTS "${path}" OR IS_SYMLINK "${path}")
    message(FATAL_ERROR "Uninstall did not remove ${path}")
  endif()
endfunction()

function(run_uninstall_case caseName withManifest)
  set(caseRoot "${TEST_ROOT}/${caseName}")
  set(home "${caseRoot}/home")
  set(dataHome "${home}/data")
  set(cacheHome "${home}/cache")
  set(configHome "${home}/config")
  set(stateHome "${home}/state")

  # Set the install destinations used when expanding the uninstall template
  set(CMAKE_CURRENT_BINARY_DIR "${caseRoot}/build")
  set(CMAKE_INSTALL_FULL_BINDIR "${caseRoot}/prefix/bin")
  set(CMAKE_INSTALL_FULL_DATAROOTDIR "${caseRoot}/prefix/share")

  set(installedPaths
      "${CMAKE_INSTALL_FULL_BINDIR}/chatgpt-desktop-unix"
      "${CMAKE_INSTALL_FULL_DATAROOTDIR}/applications/chatgpt-desktop-unix.desktop"
      "${CMAKE_INSTALL_FULL_DATAROOTDIR}/pixmaps/chatgpt-desktop-unix.png"
      "${CMAKE_INSTALL_FULL_DATAROOTDIR}/icons/hicolor/256x256/apps/chatgpt-desktop-unix.png"
      "${CMAKE_INSTALL_FULL_DATAROOTDIR}/licenses/chatgpt-desktop-unix/LICENSE"
  )
  set(applicationStatePaths
      "${dataHome}/chatgpt-desktop-unix/session"
      "${cacheHome}/chatgpt-desktop-unix/cache"
      "${configHome}/chatgpt-desktop-unix/settings"
      "${configHome}/chatgpt-desktop-unixrc"
      "${stateHome}/chatgpt-desktop-unix/window"
      "${stateHome}/chatgpt-desktop-unixstaterc"
  )

  file(REMOVE_RECURSE "${caseRoot}")
  foreach(path IN LISTS installedPaths applicationStatePaths)
    get_filename_component(parent "${path}" DIRECTORY)
    file(MAKE_DIRECTORY "${parent}")
    file(WRITE "${path}" "temporary test file")
  endforeach()

  # A clean/reconfigured build has no manifest and must still uninstall safely
  if(withManifest)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    string(REPLACE ";" "\n" manifestContents "${installedPaths}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/install_manifest.txt" "${manifestContents}\n")
  endif()

  configure_file(
      "${SOURCE_DIR}/cmake_uninstall.cmake.in"
      "${caseRoot}/cmake_uninstall.cmake"
      @ONLY
  )
  execute_process(
      COMMAND "${CMAKE_COMMAND}" -E env
          "HOME=${home}"
          "XDG_DATA_HOME=${dataHome}"
          "XDG_CACHE_HOME=${cacheHome}"
          "XDG_CONFIG_HOME=${configHome}"
          "XDG_STATE_HOME=${stateHome}"
          "${CMAKE_COMMAND}" -P "${caseRoot}/cmake_uninstall.cmake"
      RESULT_VARIABLE uninstallResult
  )
  if(NOT uninstallResult EQUAL 0)
    message(FATAL_ERROR "Uninstall case ${caseName} failed")
  endif()

  foreach(path IN LISTS installedPaths applicationStatePaths)
    assert_removed("${path}")
  endforeach()
  assert_removed("${CMAKE_INSTALL_FULL_DATAROOTDIR}/licenses/chatgpt-desktop-unix")
endfunction()

file(REMOVE_RECURSE "${TEST_ROOT}")
run_uninstall_case("manifest" TRUE)
run_uninstall_case("fallback" FALSE)
file(REMOVE_RECURSE "${TEST_ROOT}")
