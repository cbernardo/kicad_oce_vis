# This is a modified from KiCad's top-level CMakeLists.txt
# file and is intended to help build the scenegraph code
# which is taken from the KiCad branch 3d_initial_merge.

# Default to CMAKE_BUILD_TYPE = Release unless overridden on command line
# http://www.cmake.org/pipermail/cmake/2008-September/023808.html
if( DEFINED CMAKE_BUILD_TYPE )
    set( CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE} CACHE STRING "Set to either \"Release\" or \"Debug\"" )
else()
    set( CMAKE_BUILD_TYPE Release CACHE STRING "Set to either \"Release\" or \"Debug\"" )
endif()

project( oce_vis )

cmake_minimum_required( VERSION 2.8.12 FATAL_ERROR )

# Path to local CMake modules.
set( CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/CMakeModules )

# Global setting: exports are explicit
set( CMAKE_CXX_VISIBILITY_PRESET "hidden" )
set( CMAKE_VISIBILITY_INLINES_HIDDEN ON )

# Global setting: build everything position independent
set( CMAKE_POSITION_INDEPENDENT_CODE ON )


# CMP0063: CMake < 3.3 does not handle hidden visibility for static libraries,
# and 3.3 is backwards compatible when the minimum version is smaller than 3.3.
if( POLICY CMP0063 )
    cmake_policy( GET CMP0063 VISIBILITY_POLICY )
    if( VISIBILITY_POLICY STREQUAL NEW )
        message( WARNING "Compatibility code for CMake < 3.3 can be removed, search for CMP0063" )
    else()
        cmake_policy( SET CMP0063 NEW )
    endif()
else()
    if( CMAKE_CXX_COMPILE_OPTIONS_VISIBILITY AND NOT APPLE )
        set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_COMPILE_OPTIONS_VISIBILITY}hidden" )
    endif()
    if( CMAKE_CXX_COMPILE_OPTIONS_VISIBILITY_INLINES_HIDDEN AND NOT APPLE )
        set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_COMPILE_OPTIONS_VISIBILITY_INLINES_HIDDEN}" )
    endif()
endif()


# Add option to add user directories for linker, if any
LINK_DIRECTORIES( ${LINK_DIRECTORIES_PATH} )


#================================================
# Set flags for GCC, or treat llvm as GCC
#================================================

if( CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang" )

    execute_process( COMMAND ${CMAKE_C_COMPILER} -dumpversion
        OUTPUT_VARIABLE GCC_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE )

    if( CMAKE_CXX_COMPILER_ID MATCHES "Clang" )
        set( TO_LINKER -XLinker )
    else()
        set( TO_LINKER -Wl )
    endif()

    # Establish -Wall early, so specialized relaxations of this may come
    # subsequently on the command line, such as in pcbnew/github/CMakeLists.txt
    set( CMAKE_C_FLAGS   "-Wall ${CMAKE_C_FLAGS}" )
    set( CMAKE_CXX_FLAGS "-Wall ${CMAKE_CXX_FLAGS}" )

    set( CMAKE_C_FLAGS_DEBUG   "-g3 -ggdb3 -DDEBUG" )
    set( CMAKE_CXX_FLAGS_DEBUG "-g3 -ggdb3 -DDEBUG -Wno-deprecated-declarations" )

    set( CMAKE_C_FLAGS_RELEASE   "${CMAKE_C_FLAGS_RELEASE} -DNDEBUG" )
    set( CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -DNDEBUG" )

    if( MINGW )
        set( CMAKE_EXE_LINKER_FLAGS_RELEASE "-s" )

        # _UNICODE definition seems needed under mingw/gcc 4.8
        # (Kicad uses unicode, and on Windows, wxWidgets >= 2.9.4 is mandatory
        # and uses unicode)
        if( GCC_VERSION VERSION_EQUAL 4.8.0 OR GCC_VERSION VERSION_GREATER 4.8.0 )
            add_definitions(-D_UNICODE)
        endif()

        # Since version 2.8.5, Cmake uses a response file (.rsp) to
        # pass the list of include paths to gcc
        # unfortunately, under mingw32+msys, at least with gcc 4.8 and previous,
        # this file is not always expanded and in this case include paths defined in this file
        # are not taken in account ( this is the case of wxWidgets includes )
        # If it is the case, disable this response file for includes ( See Windows-GNU.cmake module )
        if( false ) #set to true to disable the include response file
            if( WIN32 AND MSYS AND NOT CMAKE_CROSSCOMPILING )
                # fixme: it is needed only with MSYS+MINGW32? or always under MINGW
                if( ${CMAKE_SIZEOF_VOID_P} MATCHES 4 )
                    set( CMAKE_CXX_USE_RESPONSE_FILE_FOR_INCLUDES 0 )
                endif()
            endif()
        endif()

        # for some reasons, cmake does do use always a response file to send the list of objects
        # to the archiver, and because this list can be very long, and can create issue
        # when it is used in a command line, force use of a response file to store it
        SET( CMAKE_CXX_USE_RESPONSE_FILE_FOR_OBJECTS 1 )

        # The MinGW compiler can use the microsoft system snprintf as standard and it has a broken
        # API with respect to the C99 standard, so make sure we force it to use its own compliant
        # snprintf
        add_definitions(-D__USE_MINGW_ANSI_STDIO=1)

    else()
        if( NOT APPLE )
            # Thou shalt not link vaporware and tell us it's a valid DSO (apple ld doesn't support it)
            set( CMAKE_SHARED_LINKER_FLAGS "${TO_LINKER},--no-undefined" )
            set( CMAKE_MODULE_LINKER_FLAGS "${TO_LINKER},--no-undefined" )

            set( CMAKE_EXE_LINKER_FLAGS_RELEASE "-s" )

            # Defeat ELF's ability to use the GOT to replace locally implemented functions
            # with ones from another module.
            # https://bugs.launchpad.net/kicad/+bug/1322354
            set( CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${TO_LINKER},-Bsymbolic" )
            set( CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} ${TO_LINKER},-Bsymbolic" )
        endif()

    endif()

    # quiet GCC while in boost
    if( GCC_VERSION VERSION_GREATER 4.8 OR GCC_VERSION VERSION_EQUAL 4.8 OR CMAKE_CXX_COMPILER_ID MATCHES "Clang" )
        set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-local-typedefs" )
    endif()
    set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-strict-aliasing" )

    if( APPLE )
        set( CMAKE_LD_FLAGS "${CMAKE_LD_FLAGS} -headerpad_max_install_names") # needed by fixbundle
    endif()

endif( CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang" )


#================================================
# Locations for install targets.
#================================================
if( NOT APPLE )
    # Everything without leading / is relative to CMAKE_INSTALL_PREFIX.
    set( KICAD_BIN ${CMAKE_INSTALL_PREFIX}/bin
        CACHE PATH "Location of binaries." )

    if( WIN32 )
        set( KICAD_LIB ${KICAD_BIN}
            CACHE PATH "Location of KiCad shared objects" )
    else()
        set( KICAD_LIB ${CMAKE_INSTALL_PREFIX}/lib
            CACHE PATH "Location of KiCad shared objects" )
    endif()

else()
    # everything without leading / is relative to CMAKE_INSTALL_PREFIX.
    # CMAKE_INSTALL_PREFIX is root of .dmg image
    set( KICAD_BIN ${CMAKE_INSTALL_PREFIX}
        CACHE PATH "Location of KiCad binaries." FORCE )

    # some paths to single app bundle
    set( OSX_BUNDLE_MAIN "kicad.app" )
    set( OSX_BUNDLE_BIN_DIR "Contents/MacOS" )
    set( OSX_BUNDLE_LIB_DIR "Contents/Frameworks" )
    set( OSX_BUNDLE_SUP_DIR "Contents/SharedSupport" )
    set( OSX_BUNDLE_APP_DIR "Contents/Applications" )
    set( OSX_BUNDLE_BUILD_DIR "${CMAKE_BINARY_DIR}/kicad/${OSX_BUNDLE_MAIN}" )
    set( OSX_BUNDLE_BUILD_BIN_DIR "${OSX_BUNDLE_BUILD_DIR}/${OSX_BUNDLE_BIN_DIR}" )
    set( OSX_BUNDLE_BUILD_LIB_DIR "${OSX_BUNDLE_BUILD_DIR}/${OSX_BUNDLE_LIB_DIR}" )
    set( OSX_BUNDLE_BUILD_KIFACE_DIR "${OSX_BUNDLE_BUILD_DIR}/${OSX_BUNDLE_KIFACE_DIR}" )
    set( OSX_BUNDLE_BUILD_PLUGIN_DIR "${OSX_BUNDLE_BUILD_DIR}/${OSX_BUNDLE_KIFACE_DIR}" )
    set( OSX_BUNDLE_INSTALL_DIR "${KICAD_BIN}/${OSX_BUNDLE_MAIN}" )
    set( OSX_BUNDLE_INSTALL_BIN_DIR "${OSX_BUNDLE_INSTALL_DIR}/${OSX_BUNDLE_BIN_DIR}" )
    set( OSX_BUNDLE_INSTALL_LIB_DIR "${OSX_BUNDLE_INSTALL_DIR}/${OSX_BUNDLE_LIB_DIR}" )
    set( OSX_BUNDLE_INSTALL_KIFACE_DIR "${OSX_BUNDLE_INSTALL_DIR}/${OSX_BUNDLE_KIFACE_DIR}" )
    set( OSX_BUNDLE_INSTALL_PLUGIN_DIR "${OSX_BUNDLE_INSTALL_DIR}/${OSX_BUNDLE_KIFACE_DIR}" )

    # everything provided with the application bundle goes into
    # kicad.app/Contents/SharedSupport => accessible via GetDataDir()
    # everything else to the .dmg image
    set( KICAD_LIB ${OSX_BUNDLE_INSTALL_DIR}/${OSX_BUNDLE_LIB_DIR}
        CACHE PATH "Location of KiCad shared objects" FORCE )
    set( KICAD_USER_PLUGIN ${OSX_BUNDLE_INSTALL_PLUGIN_DIR}
        CACHE PATH "Location of KiCad user-loaded plugins" FORCE )

    # RPATH setttings for building shared libraries
    set( CMAKE_MACOSX_RPATH FALSE )

    # Override default paths for fixup_bundle
    set( OSX_BUNDLE_OVERRIDE_PATHS "
        function( gp_item_default_embedded_path_override item default_embedded_path_var )
            # by default, embed things right next to the main bundle executable:
            set( path \"@executable_path/../../Contents/MacOS\" )
            set( overridden 0 )

            # embed .dylibs right next to the main bundle executable:
            if( item MATCHES \"\\\\.dylib$\" )
                set( path \"@executable_path/../Frameworks\" )
                set( overridden 1 )
            endif()

            set( \${default_embedded_path_var} \"\${path}\" PARENT_SCOPE )
        endfunction(gp_item_default_embedded_path_override)

        # If `BU_CHMOD_BUNDLE_ITEMS` is not set, `install_name_tool` will fail to re-write some
        # loader paths due to lack of writable permissions if the build dependencies were installed
        # by brew (or didn't have writable permissions)
        set ( BU_CHMOD_BUNDLE_ITEMS ON )
        "
        )
endif()

mark_as_advanced( KICAD_BIN
    KICAD_PLUGINS
    KICAD_USER_PLUGIN
    KICAD_LIB
    KICAD_DATA
    KICAD_DOCS
    KICAD_DEMOS
    KICAD_TEMPLATE )

include( Functions )

include( ExternalProject )

#================================================
# Find libraries that are needed to build KiCad.
#================================================
include( CheckFindPackageResult )

#
# Find wxWidgets library, required
#

# Here you can define what libraries of wxWidgets you need for your
# application. You can figure out what libraries you need here;
# http://www.wxwidgets.org/manuals/2.8/wx_librarieslist.html

# Turn on wxWidgets compatibility mode for some classes
add_definitions( -DWX_COMPATIBILITY )

# See line 41 of CMakeModules/FindwxWidgets.cmake
set( wxWidgets_CONFIG_OPTIONS ${wxWidgets_CONFIG_OPTIONS} --static=no )

find_package( wxWidgets 3.0.0 COMPONENTS gl aui adv html core net base xml stc REQUIRED )

# Include wxWidgets macros.
include( ${wxWidgets_USE_FILE} )

# Dick 5-Feb-2014:
# Marco: We cannot use both ExternalProject_Add() add and find_package()
# in the same CMake tree and have them both reference the same package:
# http://stackoverflow.com/questions/6351609/cmake-linking-to-library-downloaded-from-externalproject-add
# https://www.mail-archive.com/cmake@cmake.org/msg47501.html

# Handle target used to specify if a target needs wx-widgets or other libraries
# Always defined, empty if no libraries are to be built
add_custom_target( lib-dependencies )

#
# Find GLM library, required
#
find_package( GLM 0.9.6.0 REQUIRED )

set( LIBS_OCE PTKernel TKBin TKBinL TKBinXCAF TKBO
    TKBool TKBRep TKCAF TKCDF TKFeat TKFillet TKG2d TKG3d TKGeomAlgo
    TKGeomBase TKHLR TKIGES TKLCAF TKMath TKMesh TKMeshVS TKOffset
    TKPCAF TKPLCAF TKPrim TKPShape TKService TKShapeSchema TKShHealing
    TKStdLSchema TKStdSchema TKSTEP TKSTEP209 TKSTEPAttr TKSTEPBase
    TKSTL TKTopAlgo TKV3d TKVRML TKXCAF TKXCAFSchema TKXDEIGES
    TKXDESTEP TKXml TKXmlL TKXmlXCAF TKXSBase TKernel )

find_package( OCE 0.16 REQUIRED ${LIBS_OCE} )

# Include MinGW resource compiler.
include( MinGWResourceCompiler )

# Assist with header file searching optimization:
# INC_BEFORE and INC_AFTER are two lists which go at the front and back of the
# header file search lists, respectively.
# INC_BEFORE is for use with "include_directories( BEFORE ...)", which _reverses_
# the order during insertion.  (So put first wanted last, which is
# ${CMAKE_SOURCE_DIR/include.)  Use '.' for current source dir since
# we don't want expansion here and now, which would happen if using ${CMAKE_CURRENT_SOURCE_DIR}.
# Instead we use '.' which is applicable to any source directory below here as a result of
# this lack of expansion.
set( INC_BEFORE
    .
    ${CMAKE_SOURCE_DIR}/include
    )

set( INC_AFTER
    ${CMAKE_BINARY_DIR}
    )


if( APPLE )
    # Remove app bundles in ${KICAD_BIN} before installing anything new.
    # Must be defined before all includes so that it is executed first.
    install( CODE "
            message( STATUS \"Removing existing application bundles...\" )
            # Remove links to standalone apps
            file( REMOVE ${KICAD_BIN}/oce_vis.app )
            # Remove main bundle
            file( REMOVE_RECURSE ${KICAD_BIN}/${OSX_BUNDLE_MAIN} )
        " COMPONENT Runtime
        )
endif()


###
# Generate Map file
###
if( KICAD_MAKE_LINK_MAPS )
    # Currently only works on linux/gcc
    if( UNIX AND NOT APPLE )
        set( MAKE_LINK_MAPS true )
    else()
        set( MAKE_LINK_MAPS false )
    endif()
endif()

#================================================
# "make uninstall" rules
#================================================
configure_file(
    "${CMAKE_MODULE_PATH}/cmake_uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
    IMMEDIATE @ONLY )

add_custom_target( uninstall
    "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake" )


#================================================
# Let CMake look in these directories for nested
# 'CMakeLists.txt' files to process
#================================================

include_directories( include scenegraph ${wxWidgets_INCLUDE_DIRS} )
add_subdirectory( scenegraph/3d_cache/sg )

include_directories( ${OCE_INCLUDE_DIRS} )
add_executable( oce_vis convert.cpp )
target_link_libraries( oce_vis kicad_3dsg ${LIBS_OCE} )

install( TARGETS
    oce_vis
    DESTINATION ${KICAD_BIN}
    COMPONENT binary
    )

