// SPDX-FileCopyrightText: Copyright 2017 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <future>
#include <QColor>
#include <QImage>
#include <QList>
#include <QLocale>
#include <QMetaType>
#include <QTime>
#include <QtConcurrent/QtConcurrentRun>
#include "common/logging/log.h"
#include "network/announce_multiplayer_session.h"
#include "ui_client_room.h"
#include "yuzu/game_list_p.h"
#include "yuzu/multiplayer/client_room.h"
#include "yuzu/multiplayer/message.h"
#include "yuzu/multiplayer/moderation_dialog.h"
#include "yuzu/multiplayer/state.h"

ClientRoomWindow::ClientRoomWindow(QWidget* parent)
    : QDialog(parent, Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowSystemMenuHint),
      ui(std::make_unique<Ui::ClientRoom>()) {
    ui->setupUi(this);
    ui->chat->Initialize();

    // setup the callbacks for network updates
    if (auto member = Network::GetRoomMember().lock()) {
        member->BindOnRoomInformationChanged(
            [this](const Network::RoomInformation& info) { emit RoomInformationChanged(info); });
        member->BindOnStateChanged(
            [this](const Network::RoomMember::State& state) { emit StateChanged(state); });

        connect(this, &ClientRoomWindow::RoomInformationChanged, this,
                &ClientRoomWindow::OnRoomUpdate);
        connect(this, &ClientRoomWindow::StateChanged, this, &::ClientRoomWindow::OnStateChange);
        // Update the state
        OnStateChange(member->GetState());
    } else {
        // TODO (jroweboy) network was not initialized?
    }

    connect(ui->disconnect, &QPushButton::clicked, this, &ClientRoomWindow::Disconnect);
    ui->disconnect->setDefault(false);
    ui->disconnect->setAutoDefault(false);
    connect(ui->moderation, &QPushButton::clicked, [this] {
        ModerationDialog dialog(this);
        dialog.exec();
    });
    ui->moderation->setDefault(false);
    ui->moderation->setAutoDefault(false);
    connect(ui->chat, &ChatRoom::UserPinged, this, &ClientRoomWindow::ShowNotification);
    UpdateView();
}

ClientRoomWindow::~ClientRoomWindow() = default;

void ClientRoomWindow::SetModPerms(bool is_mod) {
    ui->chat->SetModPerms(is_mod);
    ui->moderation->setVisible(is_mod);
    ui->moderation->setDefault(false);
    ui->moderation->setAutoDefault(false);
}

void ClientRoomWindow::RetranslateUi() {
    ui->retranslateUi(this);
    ui->chat->RetranslateUi();
}

void ClientRoomWindow::OnRoomUpdate(const Network::RoomInformation& info) {
    UpdateView();
}

void ClientRoomWindow::OnStateChange(const Network::RoomMember::State& state) {
    if (state == Network::RoomMember::State::Joined ||
        state == Network::RoomMember::State::Moderator) {
        ui->chat->Clear();
        ui->chat->AppendStatusMessage(tr("Connected"));
        SetModPerms(state == Network::RoomMember::State::Moderator);
    }
    UpdateView();
}

void ClientRoomWindow::Disconnect() {
    auto parent = static_cast<MultiplayerState*>(parentWidget());
    if (parent->OnCloseRoom()) {
        ui->chat->AppendStatusMessage(tr("Disconnected"));
        close();
    }
}

void ClientRoomWindow::UpdateView() {
    if (auto member = Network::GetRoomMember().lock()) {
        if (member->IsConnected()) {
            ui->chat->Enable();
            ui->disconnect->setEnabled(true);
            auto memberlist = member->GetMemberInformation();
            ui->chat->SetPlayerList(memberlist);
            const auto information = member->GetRoomInformation();
            setWindowTitle(QString(tr("%1 - %2 (%3/%4 members) - connected"))
                               .arg(QString::fromStdString(information.name))
                               .arg(QString::fromStdString(information.preferred_game.name))
                               .arg(memberlist.size())
                               .arg(information.member_slots));
            ui->description->setText(QString::fromStdString(information.description));
            return;
        }
    }
    // TODO(B3N30): can't get RoomMember*, show error and close window
    close();
}

void ClientRoomWindow::UpdateIconDisplay() {
    ui->chat->UpdateIconDisplay();
}
