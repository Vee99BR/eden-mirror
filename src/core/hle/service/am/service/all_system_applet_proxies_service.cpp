// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core.h"
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/am/service/all_system_applet_proxies_service.h"
#include "core/hle/service/am/service/application_proxy.h"
#include "core/hle/service/am/service/library_applet_proxy.h"
#include "core/hle/service/am/service/system_applet_proxy.h"
#include "core/hle/service/am/window_system.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::AM {

IAllSystemAppletProxiesService::IAllSystemAppletProxiesService(Core::System& system_,
                                                               WindowSystem& window_system)
    : ServiceFramework{system_, "appletAE"}, m_window_system{window_system} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {100, D<&IAllSystemAppletProxiesService::OpenSystemAppletProxy>, "OpenSystemAppletProxy"},
        {110, D<&IAllSystemAppletProxiesService::Unknown110>, "Unknown110"},
        {200, D<&IAllSystemAppletProxiesService::OpenLibraryAppletProxyOld>, "OpenLibraryAppletProxyOld"},
        {201, D<&IAllSystemAppletProxiesService::OpenLibraryAppletProxy>, "OpenLibraryAppletProxy"},
        {300, nullptr, "OpenOverlayAppletProxy"},
        {350, D<&IAllSystemAppletProxiesService::OpenSystemApplicationProxy>, "OpenSystemApplicationProxy"},
        {400, nullptr, "CreateSelfLibraryAppletCreatorForDevelop"},
        {410, nullptr, "GetSystemAppletControllerForDebug"},
        {450, D<&IAllSystemAppletProxiesService::GetSystemProcessCommonFunctions>, "GetSystemProcessCommonFunctions"}, // 19.0.0+
        {460, D<&IAllSystemAppletProxiesService::Unknown460>, "Unknown460"},
        {1000, nullptr, "GetDebugFunctions"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IAllSystemAppletProxiesService::~IAllSystemAppletProxiesService() = default;

Result IAllSystemAppletProxiesService::OpenSystemAppletProxy(
    Out<SharedPointer<ISystemAppletProxy>> out_system_applet_proxy, ClientProcessId pid,
    InCopyHandle<Kernel::KProcess> process_handle) {
    LOG_DEBUG(Service_AM, "called");

    if (const auto applet = this->GetAppletFromProcessId(pid); applet) {
        *out_system_applet_proxy = std::make_shared<ISystemAppletProxy>(
            system, applet, process_handle.Get(), m_window_system);
        R_SUCCEED();
    } else {
        UNIMPLEMENTED();
        R_THROW(ResultUnknown);
    }
}

Result IAllSystemAppletProxiesService::Unknown110(
    Out<SharedPointer<ISystemAppletProxy>> out_proxy, ClientProcessId pid) {
    LOG_DEBUG(Service_AM, "called");

    auto process = system.ApplicationProcess();
    if (!process) {
        LOG_ERROR(Service_AM, "No application process available");
        R_THROW(ResultUnknown);
    }

    if (const auto applet = GetAppletFromProcessId(pid)) {
        *out_proxy = std::make_shared<ISystemAppletProxy>(
            system, applet, process, m_window_system);
        R_SUCCEED();
    }

    LOG_ERROR(Service_AM, "Applet not found for pid={}", pid.pid);
    R_THROW(ResultUnknown);
}

Result IAllSystemAppletProxiesService::OpenLibraryAppletProxy(
    Out<SharedPointer<ILibraryAppletProxy>> out_library_applet_proxy, ClientProcessId pid,
    InCopyHandle<Kernel::KProcess> process_handle,
    InLargeData<AppletAttribute, BufferAttr_HipcMapAlias> attribute) {
    LOG_DEBUG(Service_AM, "called");

    if (const auto applet = this->GetAppletFromProcessId(pid); applet) {
        *out_library_applet_proxy = std::make_shared<ILibraryAppletProxy>(
            system, applet, process_handle.Get(), m_window_system);
        R_SUCCEED();
    } else {
        UNIMPLEMENTED();
        R_THROW(ResultUnknown);
    }
}

Result IAllSystemAppletProxiesService::OpenSystemApplicationProxy(
    Out<SharedPointer<IApplicationProxy>> out_system_application_proxy, ClientProcessId pid,
    InCopyHandle<Kernel::KProcess> process_handle,
    InLargeData<AppletAttribute, BufferAttr_HipcMapAlias> attribute) {
    LOG_DEBUG(Service_AM, "called");

    if (const auto applet = this->GetAppletFromProcessId(pid); applet) {
        *out_system_application_proxy = std::make_shared<IApplicationProxy>(
            system, applet, process_handle.Get(), m_window_system);
        R_SUCCEED();
    } else {
        UNIMPLEMENTED();
        R_THROW(ResultUnknown);
    }
}

Result IAllSystemAppletProxiesService::OpenLibraryAppletProxyOld(
    Out<SharedPointer<ILibraryAppletProxy>> out_library_applet_proxy, ClientProcessId pid,
    InCopyHandle<Kernel::KProcess> process_handle) {
    LOG_DEBUG(Service_AM, "called");

    AppletAttribute attribute{};
    R_RETURN(
        this->OpenLibraryAppletProxy(out_library_applet_proxy, pid, process_handle, attribute));
}

Result IAllSystemAppletProxiesService::GetSystemProcessCommonFunctions() {
    LOG_DEBUG(Service_AM, "(STUBBED) called.");

    // TODO (jarrodnorwell)

    R_SUCCEED();
}
Result IAllSystemAppletProxiesService::Unknown460(
    Out<SharedPointer<IAppletCommonFunctions>> out_common_functions) {

    LOG_DEBUG(Service_AM, "called");

    // Получаем текущий процесс
    auto process = system.ApplicationProcess();
    if (!process) {
        LOG_ERROR(Service_AM, "No application process available");
        R_THROW(ResultUnknown);
    }

    // Получаем applet через существующий метод
    auto applet = GetAppletFromProcessId(ProcessId{process->GetProcessId()});
    if (!applet) {
        LOG_WARNING(Service_AM, "Applet not found for process");
        R_THROW(ResultUnknown);
    }

    *out_common_functions = std::make_shared<IAppletCommonFunctions>(system, applet);
    R_SUCCEED();
}
std::shared_ptr<Applet> IAllSystemAppletProxiesService::GetAppletFromProcessId(
    ProcessId process_id) {
    return m_window_system.GetByAppletResourceUserId(process_id.pid);
}

} // namespace Service::AM
