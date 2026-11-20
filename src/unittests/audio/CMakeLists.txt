# SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
# SPDX-License-Identifier: MIT

create_test(
  NAME AudioConfigTests
  DEPENDS audio
  SOURCE AudioConfigTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/audio"
)

create_test(
  NAME AudioCodecTests
  DEPENDS audio
  LIBS base
  SOURCE AudioCodecTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/audio"
)

create_test(
  NAME AudioProtocolTests
  DEPENDS audio
  LIBS base io app
  SOURCE AudioProtocolTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/audio"
)
