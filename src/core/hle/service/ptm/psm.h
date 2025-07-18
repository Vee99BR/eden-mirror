// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Service::PTM {

class PSM final : public ServiceFramework<PSM> {
public:
    explicit PSM(Core::System& system_);
    ~PSM() override;

private:
    enum class ChargerType : u32 {
        Unplugged = 0,
        RegularCharger = 1,
        LowPowerCharger = 2,
        Unknown = 3,
    };

    void GetBatteryChargePercentage(HLERequestContext& ctx);
    void GetChargerType(HLERequestContext& ctx);
    void OpenSession(HLERequestContext& ctx);
};

} // namespace Service::PTM
