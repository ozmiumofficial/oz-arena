CMAKE_MINIMUM_REQUIRED(VERSION 3.12)

PROJECT(quake3e)

# Oz: build_engine.py names them OzArena / OzArena.ded
SET(CNAME "quake3e" CACHE STRING "client executable and renderer prefix")
SET(DNAME "quake3e.ded" CACHE STRING "dedicated server executable")

SET(RENDERER_PREFIX ${CNAME})
SET(RENDERER_DEFAULT oz) # Oz: the oz renderer (rend2) draws Oz's look; valid options: oz, opengl, vulkan

OPTION(USE_SDL "" ON)
OPTION(USE_CURL "" ON)
OPTION(USE_LOCAL_HEADERS "" ON)
OPTION(USE_VULKAN "" ON)
OPTION(USE_SYSTEM_JPEG "" OFF)
OPTION(USE_RENDERER_DLOPEN "" ON)

SET(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules)

IF(UNIX AND NOT APPLE)
	# Oz: the Linux client (2026-11-04). The renderer modules are shared objects with static libraries
	# (Jolt) in them, so everything is position independent; the executables find the SDL2 and OpenAL
	# Soft built beside them ($ORIGIN), and carry their own C++ runtime, for distros older than the build's.
	SET(CMAKE_POSITION_INDEPENDENT_CODE ON)
	SET(CMAKE_BUILD_RPATH "\$ORIGIN")
	SET(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
	SET(CMAKE_INSTALL_RPATH "\$ORIGIN")
	IF(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")	# zig's clang (build_engine.py --linux) links its libc++ in already
		SET(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libstdc++ -static-libgcc")
		SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++ -static-libgcc")
	ENDIF()
ENDIF()
IF(UNIX)
	SET(THREADS_PREFER_PTHREAD_FLAG ON)
	FIND_PACKAGE(Threads REQUIRED)
ENDIF()

AUX_SOURCE_DIRECTORY(code/qcommon QCOMMON_SRCS)
# exclude platform-dependent QVM bytecode compilers
list(FILTER QCOMMON_SRCS EXCLUDE REGEX ".*vm_[alpx].*.c")
# add platform-dependent QVM bytecode compilers
IF(CMAKE_SYSTEM_PROCESSOR MATCHES AMD64|x86|i*86)
	IF(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")	# Oz: Linux says x86_64
		IF(WIN32)
			SET(BINEXT .x64)
		ELSE()
			SET(BINEXT .x86_64)
		ENDIF()
		SET(RENDEXT _x86_64)
	ELSE()
		#SET(BINEXT .x86)
		SET(RENDEXT _x86)
	ENDIF()
	list(APPEND QCOMMON_SRCS code/qcommon/vm_x86.c)
ELSEIF(CMAKE_SYSTEM_PROCESSOR MATCHES aarch64|arm64)
	SET(BINEXT .aarch64)
	SET(RENDEXT _aarch64)
	list(APPEND QCOMMON_SRCS code/qcommon/vm_aarch64.c)
ELSEIF(CMAKE_SYSTEM_PROCESSOR MATCHES arm*)
	SET(BINEXT .arm)
	SET(RENDEXT _arm)
	list(APPEND QCOMMON_SRCS code/qcommon/vm_armv7l.c)
ELSEIF(CMAKE_SYSTEM_PROCESSOR MATCHES "ppc64le|ppc64")
	IF(CMAKE_SYSTEM_PROCESSOR MATCHES "ppc64le")
		SET(BINEXT .ppc64le)
		SET(RENDEXT _ppc64le)
		list(APPEND QCOMMON_SRCS code/qcommon/vm_powerpc.c)
	ELSE()
		SET(BINEXT .ppc64)
		SET(RENDEXT _ppc64)
		list(APPEND QCOMMON_SRCS code/qcommon/vm_powerpc.c)
	ENDIF()
	ADD_COMPILE_OPTIONS(-mcpu=power8 -mvsx)
ELSE()
	TARGET_COMPILE_DEFINITIONS(qcommon PRIVATE -DNO_VM_COMPILED)
	TARGET_COMPILE_DEFINITIONS(qcommon_ded PRIVATE -DNO_VM_COMPILED)
ENDIF()

# botlib
AUX_SOURCE_DIRECTORY(code/botlib BOTLIB_SRCS)
ADD_LIBRARY(botlib OBJECT ${BOTLIB_SRCS})
TARGET_COMPILE_DEFINITIONS(botlib PRIVATE BOTLIB)

AUX_SOURCE_DIRECTORY(code/server SERVER_SRCS)
list(REMOVE_ITEM SERVER_SRCS code/server/sv_rankings.c)
list(APPEND SERVER_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/../src/oz/replay/sv_oz_replay.c)	# Oz: the replay recorder (P10)

# MSVC x64 has no inline asm: setjmp/longjmp and CPUID come from MASM
IF(MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES AMD64)
	ENABLE_LANGUAGE(ASM_MASM)
	list(APPEND QCOMMON_SRCS code/asm/common_x64.asm)
ENDIF()

# universal binary
ADD_LIBRARY(qcommon OBJECT ${QCOMMON_SRCS} ${SERVER_SRCS})

# dedicated server
ADD_LIBRARY(qcommon_ded OBJECT ${QCOMMON_SRCS} ${SERVER_SRCS})
TARGET_COMPILE_DEFINITIONS(qcommon_ded PUBLIC DEDICATED)

IF(NO_VM_COMPILED)
	TARGET_COMPILE_DEFINITIONS(qcommon PRIVATE NO_VM_COMPILED)
	TARGET_COMPILE_DEFINITIONS(qcommon_ded PRIVATE NO_VM_COMPILED)
ENDIF()

# client + cURL
AUX_SOURCE_DIRECTORY(code/client CLIENT_SRCS)
# Oz: the 64-bit sound mixer's SSE copy, which snd_mix.c calls on every compiler but MSVC (as the Makefile builds it)
IF(UNIX AND CMAKE_SYSTEM_PROCESSOR MATCHES x86_64)
	ENABLE_LANGUAGE(ASM)
	SET_SOURCE_FILES_PROPERTIES(code/asm/snd_mix_x86_64.s PROPERTIES LANGUAGE ASM COMPILE_OPTIONS "-x;assembler-with-cpp;-DELF")
	list(APPEND CLIENT_SRCS code/asm/snd_mix_x86_64.s)
ENDIF()
list(APPEND CLIENT_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/../src/oz/replay/cl_oz_replay.c)	# Oz: replay playback (P10)
list(APPEND CLIENT_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/../src/oz/textures/cl_oz_textures.c)	# Oz: the player's custom textures
IF(NOT USE_CURL)
	list(REMOVE_ITEM CLIENT_SRCS code/client/cl_curl.c)
ENDIF()
OPTION(USE_OPENAL "Oz: ioquake3's OpenAL backend, loaded at run time" ON)
IF(NOT USE_OPENAL)
	list(REMOVE_ITEM CLIENT_SRCS code/client/qal.c code/client/snd_openal.c)
ENDIF()

#ADD_LIBRARY(client OBJECT ${CLIENT_SRCS})

# jpeg
IF(USE_SYSTEM_JPEG)
	ADD_LIBRARY(client OBJECT ${CLIENT_SRCS})
	find_package(JPEG REQUIRED)
	TARGET_COMPILE_DEFINITIONS(client PRIVATE -DUSE_SYSTEM_JPEG)
	TARGET_LINK_LIBRARIES(client ${JPEG_LIBRARIES})
ELSE()
	AUX_SOURCE_DIRECTORY(code/libjpeg JPEG_SRCS)
	#ADD_LIBRARY(jpeg ${JPEG_SRCS})
	#TARGET_LINK_LIBRARIES(client jpeg)
	ADD_LIBRARY(client OBJECT ${CLIENT_SRCS} ${JPEG_SRCS})
ENDIF()

IF(USE_OPENAL)
	# Oz: OpenAL Soft's headers are in code/AL; the library is loaded at run time (s_alDriver)
	TARGET_COMPILE_DEFINITIONS(client PRIVATE USE_OPENAL USE_OPENAL_DLOPEN USE_INTERNAL_OPENAL_HEADERS)
	TARGET_INCLUDE_DIRECTORIES(client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/code)
ENDIF()

IF(USE_SDL)
	# Oz: the client's SDL-only parts (the controller's sticks, the monitor and audio device lists)
	TARGET_COMPILE_DEFINITIONS(client PRIVATE USE_SDL)
ENDIF()

# Oz: Ogg Vorbis music and the cinematics' sound (ENGINE_PLAN.md P3): Quake3e's bundled libogg and
# libvorbis, built in as its Makefile does (its CMake left them out)
OPTION(USE_OGG_VORBIS "" ON)
IF(USE_OGG_VORBIS)
	FILE(GLOB VORBIS_SRCS code/libvorbis/lib/*.c)
	TARGET_SOURCES(client PRIVATE code/libogg/src/bitwise.c code/libogg/src/framing.c ${VORBIS_SRCS})
	TARGET_COMPILE_DEFINITIONS(client PRIVATE USE_OGG_VORBIS)
	TARGET_INCLUDE_DIRECTORIES(client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/code/libogg/include
		${CMAKE_CURRENT_SOURCE_DIR}/code/libvorbis/include ${CMAKE_CURRENT_SOURCE_DIR}/code/libvorbis/lib)
ENDIF()
IF(THEORA_DIR)
	# Oz: the cinematics' video (cl_cin_ogv.c): libtheora's decoder, fetched by build_engine.py
	# (thirdparty.json), plain C as its Makefile.am lists it, on the bundled libogg
	FOREACH(f apiwrapper bitpack decapiwrapper decinfo decode dequant fragment huffdec idct info internal quant state)
		TARGET_SOURCES(client PRIVATE ${THEORA_DIR}/lib/${f}.c)
	ENDFOREACH()
	TARGET_COMPILE_DEFINITIONS(client PRIVATE USE_THEORA)
	TARGET_INCLUDE_DIRECTORIES(client PRIVATE ${THEORA_DIR}/include)
ELSE()
	list(REMOVE_ITEM CLIENT_SRCS code/client/cl_cin_ogv.c)
ENDIF()

IF(USE_CURL)
	# Oz: upstream's CMake never defined USE_CURL, so map auto-download (dlmap, cl_mapAutoDownload) was
	# missing. MSVC links Quake3e's bundled static libcurl (Schannel TLS) as its MSVC project does;
	# elsewhere libcurl is loaded at run time as in its Makefile.
	TARGET_COMPILE_DEFINITIONS(client PRIVATE USE_CURL)
	IF(OZ_CURL_LIBRARY)
		# Oz: libcurl 8.19 built by build_engine.py (thirdparty.json), for the online hub's WebSocket (P7);
		# Schannel's TLS on Windows, mbedTLS's on Linux (thirdparty.json's OZ_CURL_LIBRARY lists it)
		TARGET_COMPILE_DEFINITIONS(client PRIVATE CURL_STATICLIB)
		TARGET_INCLUDE_DIRECTORIES(client PRIVATE ${OZ_CURL_INCLUDE_DIR})
		IF(WIN32)
			SET(CURL_LIBRARY ${OZ_CURL_LIBRARY} ws2_32 secur32 bcrypt iphlpapi crypt32)
		ELSE()
			SET(CURL_LIBRARY ${OZ_CURL_LIBRARY} Threads::Threads)
		ENDIF()
	ELSEIF(MSVC)
		TARGET_COMPILE_DEFINITIONS(client PRIVATE CURL_STATICLIB)
		TARGET_INCLUDE_DIRECTORIES(client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/code/libcurl/windows/include)
		IF(CMAKE_SIZEOF_VOID_P EQUAL 8)
			SET(CURL_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/code/libcurl/windows/vs2017/lib64/libcurl_a.lib)
		ELSE()
			SET(CURL_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/code/libcurl/windows/vs2017/lib32/libcurl_a.lib)
		ENDIF()
		LIST(APPEND CURL_LIBRARY crypt32)
	ELSE()
		TARGET_COMPILE_DEFINITIONS(client PRIVATE USE_CURL_DLOPEN)
	ENDIF()
ENDIF()

IF(PCRE2_LIBRARY)
	# Oz: oDFe's console filters (con_filter); build_engine.py passes PCRE2 built from source
	TARGET_COMPILE_DEFINITIONS(client PRIVATE USE_PCRE2 PCRE2_STATIC)
	TARGET_INCLUDE_DIRECTORIES(client PRIVATE ${PCRE2_INCLUDE_DIR})
ENDIF()

# renderers
AUX_SOURCE_DIRECTORY(code/renderercommon RENDERER_COMMON_SRCS)
AUX_SOURCE_DIRECTORY(code/renderer RENDERER_GL_SRCS)
AUX_SOURCE_DIRECTORY(code/renderervk RENDERER_VK_SRCS)

IF(USE_RENDERER_DLOPEN)
	SET(AUX_SRCS
		code/qcommon/q_shared.c
		code/qcommon/puff.c
		code/qcommon/q_math.c
	)
	IF(MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES AMD64)
		list(APPEND AUX_SRCS code/asm/common_x64.asm)
	ENDIF()
	TARGET_COMPILE_DEFINITIONS(client PRIVATE USE_RENDERER_DLOPEN RENDERER_PREFIX="${RENDERER_PREFIX}" RENDERER_DEFAULT=${RENDERER_DEFAULT})

	ADD_LIBRARY(${RENDERER_PREFIX}_opengl${RENDEXT} SHARED ${RENDERER_GL_SRCS} ${RENDERER_COMMON_SRCS} ${AUX_SRCS})
	TARGET_COMPILE_DEFINITIONS(${RENDERER_PREFIX}_opengl${RENDEXT} PRIVATE USE_RENDERER_DLOPEN)
	SET_TARGET_PROPERTIES(${RENDERER_PREFIX}_opengl${RENDEXT} PROPERTIES PREFIX "")

	ADD_LIBRARY(${RENDERER_PREFIX}_vulkan${RENDEXT} SHARED ${RENDERER_VK_SRCS} ${RENDERER_COMMON_SRCS} ${AUX_SRCS})
	TARGET_COMPILE_DEFINITIONS(${RENDERER_PREFIX}_vulkan${RENDEXT} PRIVATE USE_RENDERER_DLOPEN)
	SET_TARGET_PROPERTIES(${RENDERER_PREFIX}_vulkan${RENDEXT} PROPERTIES PREFIX "")

	# Oz: the oz renderer is ioquake3's current rend2 in code/renderer2 (engine/RENDERER2_SYNC.md);
	# its GLSL is embedded as C strings the way ioquake3's CMake does it
	AUX_SOURCE_DIRECTORY(code/renderer2 RENDERER_OZ_SRCS)
	list(REMOVE_ITEM RENDERER_OZ_SRCS code/renderer2/stringify.c)
	FILE(GLOB RENDERER_OZ_GLSL ${CMAKE_CURRENT_SOURCE_DIR}/code/renderer2/glsl/*.glsl)
	# Oz: AMD FSR 1's shaders, made from its headers by engine/build_engine.py (fsr_glsl)
	FILE(GLOB RENDERER_OZ_FSR_GLSL ${OZ_FSR_GLSL_DIR}/*.glsl)
	list(APPEND RENDERER_OZ_GLSL ${RENDERER_OZ_FSR_GLSL})
	FOREACH(SHADER_FILE IN LISTS RENDERER_OZ_GLSL)
		GET_FILENAME_COMPONENT(SHADER_NAME ${SHADER_FILE} NAME_WE)
		SET(SHADER_C_FILE ${CMAKE_CURRENT_BINARY_DIR}/rend2_glsl/${SHADER_NAME}.c)
		ADD_CUSTOM_COMMAND(OUTPUT ${SHADER_C_FILE}
			COMMAND ${CMAKE_COMMAND} -DINPUT_FILE=${SHADER_FILE} -DOUTPUT_FILE=${SHADER_C_FILE} -DSHADER_NAME=${SHADER_NAME}
				-P ${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/stringify_shader.cmake
			DEPENDS ${SHADER_FILE})
		list(APPEND RENDERER_OZ_SRCS ${SHADER_C_FILE})
	ENDFOREACH()
	ADD_LIBRARY(${RENDERER_PREFIX}_oz${RENDEXT} SHARED ${RENDERER_OZ_SRCS} ${RENDERER_COMMON_SRCS} ${AUX_SRCS})
	TARGET_COMPILE_DEFINITIONS(${RENDERER_PREFIX}_oz${RENDEXT} PRIVATE USE_RENDERER_DLOPEN)
	SET_TARGET_PROPERTIES(${RENDERER_PREFIX}_oz${RENDEXT} PROPERTIES PREFIX "")
ELSE()
	IF(USE_VULKAN)
		ADD_LIBRARY(${RENDERER_PREFIX}_vulkan OBJECT ${RENDERER_VK_SRCS} ${RENDERER_COMMON_SRCS})
	ELSE()
		ADD_LIBRARY(${RENDERER_PREFIX}_opengl OBJECT ${RENDERER_GL_SRCS} ${RENDERER_COMMON_SRCS})
	ENDIF()
ENDIF()

# common function, window system
IF(UNIX)
	SET (Q3_SRCS
		code/unix/unix_main.c
		code/unix/unix_shared.c
		code/unix/linux_signals.c)

	IF(USE_SDL)
		AUX_SOURCE_DIRECTORY(code/sdl Q3_UI_SRCS)
	ELSE()
		SET(Q3_UI_SRCS
			code/unix/linux_glimp.c
			code/unix/linux_qgl.c
			code/unix/linux_snd.c
			code/unix/x11_dga.c
			code/unix/x11_randr.c
			code/unix/x11_vidmode.c)
		IF(USE_VULKAN)
			list(APPEND Q3_UI_SRCS code/unix/linux_qvk.c)
		ENDIF()
	ENDIF()

	IF(APPLE)
		SET(EXE_TYPE MACOSX_BUNDLE)
		list(APPEND Q3_SRCS code/unix/quake3_flat.icns)
		set_source_files_properties(code/unix/quake3_flat.icns PROPERTIES
			MACOSX_PACKAGE_LOCATION Resources)
		SET(MACOSX_BUNDLE_ICON_FILE quake3_flat.icns)
	ENDIF()
ELSEIF(WIN32)
	SET(EXE_TYPE WIN32)
	IF(MSVC)
		SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /MANIFEST:NO")
		ADD_COMPILE_DEFINITIONS(_CRT_SECURE_NO_WARNINGS) # v3.12+
	ENDIF()
	ADD_COMPILE_DEFINITIONS(USE_PROFILES) # Oz: user files in %APPDATA%\Oz Arena, not beside the exe

	SET(Q3_SRCS
		code/win32/win_main.c
		code/win32/win_shared.c
		code/win32/win_syscon.c
		code/win32/win_resource.rc)
	# Oz: rebuild the resources when the icon changes (Ninja doesn't see files an .rc pulls in)
	SET_SOURCE_FILES_PROPERTIES(code/win32/win_resource.rc PROPERTIES
		OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/code/win32/oz.ico)

	IF(USE_SDL AND (NOT MSVC OR SDL2_LIBRARY))
		# Oz: SDL's window, input, gamma and sound on Windows too (game controllers everywhere);
		# under MSVC build_engine.py passes SDL2_INCLUDE_DIRS and SDL2_LIBRARY
		AUX_SOURCE_DIRECTORY(code/sdl Q3_UI_SRCS)
	ELSE()
		SET(Q3_UI_SRCS
			code/win32/win_gamma.c
			code/win32/win_glimp.c
			code/win32/win_input.c
			code/win32/win_minimize.c
			code/win32/win_qgl.c
			code/win32/win_snd.c
			code/win32/win_wndproc.c)
		IF(USE_VULKAN)
			list(APPEND Q3_UI_SRCS code/win32/win_qvk.c)
		ENDIF()
	ENDIF()
ENDIF(UNIX)

ADD_LIBRARY(q3ui OBJECT ${Q3_UI_SRCS})

IF(USE_SDL AND SDL2_LIBRARY)	# Oz: build_engine.py's SDL2 on Windows and Linux
	TARGET_INCLUDE_DIRECTORIES(q3ui PRIVATE ${SDL2_INCLUDE_DIRS})
	TARGET_INCLUDE_DIRECTORIES(client PRIVATE ${SDL2_INCLUDE_DIRS})
	TARGET_COMPILE_DEFINITIONS(q3ui PRIVATE USE_JOYSTICK)	# Oz: SDL game controllers (Quake3e never defined it)
ELSEIF(NOT MSVC)
	IF(USE_SDL)
		find_package(SDL2 REQUIRED)
		TARGET_LINK_LIBRARIES(q3ui SDL2::Main)
		TARGET_COMPILE_DEFINITIONS(q3ui PRIVATE USE_JOYSTICK)	# Oz
	ELSEIF(UNIX)
		find_package (X11 REQUIRED)
		TARGET_LINK_LIBRARIES(q3ui ${X11_LIBRARIES})
	ENDIF()
ENDIF()

IF(USE_VULKAN)
	TARGET_COMPILE_DEFINITIONS(q3ui PRIVATE USE_VULKAN_API)
	TARGET_COMPILE_DEFINITIONS(client PRIVATE USE_VULKAN_API)
ENDIF()

IF(USE_RENDERER_DLOPEN OR NOT USE_VULKAN)
	TARGET_COMPILE_DEFINITIONS(q3ui PRIVATE USE_OPENGL_API)
	TARGET_COMPILE_DEFINITIONS(client PRIVATE USE_OPENGL_API)
ENDIF()

# client executable

ADD_EXECUTABLE(${CNAME}${BINEXT} ${EXE_TYPE} ${Q3_SRCS})
# Oz: online services (ENGINE_PLAN.md P7): the hub's WebSocket over libcurl, the identity's RSA through
# mbedTLS, in engine/src/oz/online (C++17); oz_online_engine.c is the client's side.
IF(MBEDCRYPTO_LIBRARY AND OZ_CURL_LIBRARY AND RMLUI_LIBRARY)
	SET(OZ_ONLINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src/oz/online)
	FILE(GLOB OZ_ONLINE_SRCS ${OZ_ONLINE_DIR}/*.cpp)
	ADD_LIBRARY(oz_online OBJECT ${OZ_ONLINE_SRCS})
	SET_TARGET_PROPERTIES(oz_online PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
	TARGET_INCLUDE_DIRECTORIES(oz_online PRIVATE ${MBEDTLS_INCLUDE_DIR} ${OZ_CURL_INCLUDE_DIR} ${CJSON_DIR})
	TARGET_COMPILE_DEFINITIONS(oz_online PRIVATE CURL_STATICLIB)
	TARGET_SOURCES(client PRIVATE ${OZ_ONLINE_DIR}/oz_online_engine.c ${OZ_ONLINE_DIR}/oz_stats.c ${OZ_ONLINE_DIR}/oz_update.c)	# oz_stats.c: P8b's match reports; oz_update.c: the update check
	TARGET_INCLUDE_DIRECTORIES(client PRIVATE ${CJSON_DIR})
	TARGET_COMPILE_DEFINITIONS(client PRIVATE USE_OZ_ONLINE)
	TARGET_LINK_LIBRARIES(${CNAME}${BINEXT} oz_online ${MBEDCRYPTO_LIBRARY})
	# Oz: peer-to-peer play (ENGINE_PLAN.md P8): the NA_ICE transport on libjuice, signaled through the hub
	IF(JUICE_LIBRARY)
		TARGET_SOURCES(client PRIVATE ${OZ_ONLINE_DIR}/oz_ice.c ${OZ_ONLINE_DIR}/oz_upnp.c)
		# Oz: editing together's channel on the same links (ENGINE_PLAN.md PE): KCP's reliable stream beside the game's packets
		IF(KCP_DIR)
			TARGET_SOURCES(client PRIVATE ${OZ_ONLINE_DIR}/oz_edlink.c ${KCP_DIR}/ikcp.c)
			TARGET_INCLUDE_DIRECTORIES(client PRIVATE ${KCP_DIR})
			TARGET_COMPILE_DEFINITIONS(client PRIVATE USE_OZ_EDLINK)
		ENDIF()
		TARGET_INCLUDE_DIRECTORIES(client PRIVATE ${JUICE_INCLUDE_DIR})
		TARGET_COMPILE_DEFINITIONS(client PRIVATE USE_OZ_ICE JUICE_STATIC)
		TARGET_COMPILE_DEFINITIONS(qcommon PRIVATE USE_OZ_ICE)
		IF(WIN32)
			TARGET_LINK_LIBRARIES(${CNAME}${BINEXT} ${JUICE_LIBRARY} ws2_32 bcrypt ole32 oleaut32)	# oleaut32: UPnP (oz_upnp.c)
		ELSE()
			TARGET_LINK_LIBRARIES(${CNAME}${BINEXT} ${JUICE_LIBRARY} Threads::Threads)	# UPnP is Windows' only
		ENDIF()
	ENDIF()
ENDIF()
TARGET_LINK_LIBRARIES(${CNAME}${BINEXT} qcommon botlib client q3ui)
IF(USE_SDL AND SDL2_LIBRARY)
	TARGET_LINK_LIBRARIES(${CNAME}${BINEXT} ${SDL2_LIBRARY})	# Oz
ENDIF()
IF(PCRE2_LIBRARY)
	TARGET_LINK_LIBRARIES(${CNAME}${BINEXT} ${PCRE2_LIBRARY})	# Oz: oDFe's console filters
ENDIF()
IF(CURL_LIBRARY)
	TARGET_LINK_LIBRARIES(${CNAME}${BINEXT} ${CURL_LIBRARY})	# Oz: map auto-download
ENDIF()
#TARGET_LINK_OPTIONS(${CNAME} PRIVATE -fno-pie) # v3.13+

IF(NOT USE_RENDERER_DLOPEN)
	IF(USE_VULKAN)
		TARGET_LINK_LIBRARIES(${CNAME}${BINEXT} ${RENDERER_PREFIX}_vulkan)
	ELSE()
		TARGET_LINK_LIBRARIES(${CNAME}${BINEXT} ${RENDERER_PREFIX}_opengl)
	ENDIF()
ENDIF()

# dedicated server executable

ADD_EXECUTABLE(${DNAME}${BINEXT} ${EXE_TYPE} ${Q3_SRCS})
TARGET_LINK_LIBRARIES(${DNAME}${BINEXT} qcommon_ded botlib)
#TARGET_LINK_OPTIONS(${DNAME} PRIVATE -fno-pie) # v3.13+

IF(WIN32)
	TARGET_LINK_LIBRARIES(${CNAME}${BINEXT} winmm comctl32 ws2_32 iphlpapi)
	TARGET_LINK_LIBRARIES(${DNAME}${BINEXT} winmm comctl32 ws2_32 iphlpapi)
ELSE()
	TARGET_LINK_LIBRARIES(${CNAME}${BINEXT} m ${CMAKE_DL_LIBS} Threads::Threads)
	TARGET_LINK_LIBRARIES(${DNAME}${BINEXT} m ${CMAKE_DL_LIBS} Threads::Threads)
ENDIF()

# Oz: the native UI (ENGINE_PLAN.md P4): RmlUi and FreeType built by build_engine.py, the page logic
# in engine/src/oz/ui (C++17), cJSON for ui/settings.json. oz_ui_engine.c is the client's side.
IF(RMLUI_LIBRARY)
	SET(OZ_UI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src/oz/ui)
	FILE(GLOB OZ_UI_SRCS ${OZ_UI_DIR}/*.cpp)
	ADD_LIBRARY(oz_ui OBJECT ${OZ_UI_SRCS} ${CJSON_DIR}/cJSON.c)
	SET_TARGET_PROPERTIES(oz_ui PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
	TARGET_INCLUDE_DIRECTORIES(oz_ui PRIVATE ${RMLUI_INCLUDE_DIR} ${CJSON_DIR})
	TARGET_COMPILE_DEFINITIONS(oz_ui PRIVATE RMLUI_STATIC_LIB)
	TARGET_SOURCES(client PRIVATE ${OZ_UI_DIR}/oz_ui_engine.c ${OZ_UI_DIR}/oz_ui_showcase.c ${OZ_UI_DIR}/oz_hud.c ${OZ_UI_DIR}/oz_emoji.c ${OZ_UI_DIR}/oz_hud_mods.c ${OZ_UI_DIR}/oz_hud_view.c ${OZ_UI_DIR}/oz_settings_apply.c
		${OZ_UI_DIR}/../online/oz_servers.c ${OZ_UI_DIR}/oz_mods.c	# the Server Browser's community servers and the mods players bring (P8b)
		${OZ_UI_DIR}/oz_races.c	# Defrag's race maps, for Skirmish's mod picker
		${OZ_UI_DIR}/oz_q3import.c)	# a player's Quake III config, imported
	TARGET_COMPILE_DEFINITIONS(client PRIVATE USE_OZ_UI)
	TARGET_COMPILE_DEFINITIONS(qcommon PRIVATE USE_OZ_UI)	# sv_game.c hands the campaign its postgame (P12)
	TARGET_COMPILE_DEFINITIONS(q3ui PRIVATE USE_OZ_UI)
	TARGET_LINK_LIBRARIES(${CNAME}${BINEXT} oz_ui ${RMLUI_LIBRARY} ${FREETYPE_LIBRARY})

	# Oz: the map editor (ENGINE_PLAN.md P13, engine/EDITOR.md): its core and view in engine/src/oz/editor
	# (C++17, no RmlUi), the engine's side in oz_editor_engine.c, its page in oz_ui_editor.cpp
	SET(OZ_EDITOR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src/oz/editor)
	FILE(GLOB OZ_EDITOR_SRCS ${OZ_EDITOR_DIR}/*.cpp)
	IF(MINIZ_DIR)	# Oz: deflate for editing together's snapshots and play-test paks (PE)
		LIST(APPEND OZ_EDITOR_SRCS ${MINIZ_DIR}/miniz.c ${MINIZ_DIR}/miniz_tdef.c ${MINIZ_DIR}/miniz_tinfl.c ${MINIZ_DIR}/miniz_zip.c)
	ENDIF()
	ADD_LIBRARY(oz_editor OBJECT ${OZ_EDITOR_SRCS})
	IF(MINIZ_DIR)
		TARGET_INCLUDE_DIRECTORIES(oz_editor PRIVATE ${OZ_EDITOR_DIR} ${MINIZ_DIR})
		TARGET_COMPILE_DEFINITIONS(oz_editor PRIVATE OZ_ED_MINIZ MINIZ_NO_ZLIB_COMPATIBLE_NAMES MINIZ_NO_STDIO MINIZ_NO_TIME)
	ENDIF()
	SET_TARGET_PROPERTIES(oz_editor PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
	TARGET_SOURCES(client PRIVATE ${OZ_EDITOR_DIR}/oz_editor_engine.c)
	TARGET_LINK_LIBRARIES(${CNAME}${BINEXT} oz_editor)
ENDIF()

# Oz: the fighters' ragdolls run on Jolt Physics (ENGINE_PLAN.md P2), fetched by build_engine.py and built
# here into the oz renderer: its library only, with the engine's runtime and flags (so plain SSE2, for any
# x64 PC), without its debug renderer or profiler
IF(JOLT_DIR AND TARGET ${RENDERER_PREFIX}_oz${RENDEXT})
	FOREACH(OPT USE_STATIC_MSVC_RUNTIME_LIBRARY OVERRIDE_CXX_FLAGS INTERPROCEDURAL_OPTIMIZATION ENABLE_ALL_WARNINGS GENERATE_DEBUG_SYMBOLS
			DEBUG_RENDERER_IN_DEBUG_AND_RELEASE PROFILER_IN_DEBUG_AND_RELEASE ENABLE_OBJECT_STREAM
			FLOATING_POINT_EXCEPTIONS_ENABLED ENABLE_INSTALL USE_AVX USE_AVX2 USE_LZCNT USE_TZCNT USE_F16C USE_FMADD)
		SET(${OPT} OFF CACHE BOOL "" FORCE)
	ENDFOREACH()
	ADD_SUBDIRECTORY(${JOLT_DIR}/Build ${CMAKE_CURRENT_BINARY_DIR}/jolt EXCLUDE_FROM_ALL)
	TARGET_SOURCES(${RENDERER_PREFIX}_oz${RENDEXT} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/oz/physics/oz_physics.cpp)
	TARGET_INCLUDE_DIRECTORIES(${RENDERER_PREFIX}_oz${RENDEXT} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/oz/physics)
	TARGET_LINK_LIBRARIES(${RENDERER_PREFIX}_oz${RENDEXT} Jolt)
	IF(UNIX)
		TARGET_LINK_LIBRARIES(${RENDERER_PREFIX}_oz${RENDEXT} Threads::Threads)	# Jolt's job threads
	ENDIF()
	SET_TARGET_PROPERTIES(${RENDERER_PREFIX}_oz${RENDEXT} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
	TARGET_COMPILE_DEFINITIONS(${RENDERER_PREFIX}_oz${RENDEXT} PRIVATE USE_OZ_PHYSICS)
ENDIF()
