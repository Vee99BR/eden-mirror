# SPDX-FileCopyrightText: 2023 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

add_library(frontend_common STATIC
    config.cpp
    config.h
    content_manager.h
    firmware_manager.h
    firmware_manager.cpp
)

create_target_directory_groups(frontend_common)
target_link_libraries(frontend_common PUBLIC core SimpleIni::SimpleIni PRIVATE common Boost::headers)
