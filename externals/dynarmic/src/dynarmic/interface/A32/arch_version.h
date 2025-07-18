/* This file is part of the dynarmic project.
 * Copyright (c) 2020 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <stdint.h>

namespace Dynarmic {
namespace A32 {

enum class ArchVersion : std::uint8_t {
    v3,
    v4,
    v4T,
    v5TE,
    v6K,
    v6T2,
    v7,
    v8,
};

}  // namespace A32
}  // namespace Dynarmic
