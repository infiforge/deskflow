# SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
# SPDX-License-Identifier: MIT

set(lib_name audio)

if(WIN32)
  set(PLATFORM_CODE
    AudioCaptureWin.cpp
  )
endif()

add_library(${lib_name} STATIC
  ${PLATFORM_CODE}
  AudioBuffer.cpp
  AudioConfig.cpp
  AudioCaptureFactory.cpp
  AudioCapturePulse.cpp
  AudioCaptureQt.cpp
  AudioBuffer.h
  AudioCapture.h
  AudioReceiver.cpp
  AudioReceiver.h
  AudioSender.cpp
  AudioSender.h
  AudioSink.cpp
  AudioSink.h
  AudioTypes.h
  OpusCodec.cpp
  OpusCodec.h
)

target_include_directories(${lib_name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${lib_name}
  PUBLIC
    base
    common
    Qt6::Core
    Qt6::Multimedia
    ${OPUS_LIBRARIES}
)

target_include_directories(${lib_name} PRIVATE ${OPUS_INCLUDE_DIRS})

if(WIN32)
  # version.lib is needed by arch.lib (through base) for runtime version checks.
  target_link_libraries(${lib_name} PUBLIC ole32 uuid version)
endif()
