#include "DiscordRPC.h"
#include <QFileInfo>
#include <QProcess>
#include <QDir>
#include <QDebug>
#include <qcoreapplication.h>
#include <unistd.h>

DiscordRPC::DiscordRPC(QObject* parent) : QObject(parent) {
    m_socket = new QLocalSocket(this);

    connect(m_socket, &QLocalSocket::connected, this, &DiscordRPC::onConnected);
    connect(m_socket, &QLocalSocket::disconnected, this, &DiscordRPC::onDisconnected);
    connect(m_socket, &QLocalSocket::readyRead, this, &DiscordRPC::onReadyRead);

    // Initiate connection asynchronously
    QString socketPath = findDiscordSocketPath();
    if (!socketPath.isEmpty()) {
        m_socket->connectToServer(socketPath);
    } else {
        qWarning() << "Discord local desktop socket could not be resolved.";
    }
}

DiscordRPC::~DiscordRPC() {
    if (m_socket->isOpen()) {
        m_socket->close();
    }
}

QString DiscordRPC::findDiscordSocketPath() {
    // Search the standard system mount configurations
    QString uidStr = QString::number(getuid());
    QString standardPath = QString("/run/user/%1/discord-ipc-0").arg(uidStr);
    if (QFileInfo::exists(standardPath)) return standardPath;

    QString tmpPath = "/tmp/discord-ipc-0";
    if (QFileInfo::exists(tmpPath)) return tmpPath;

    // Support for Discord instances installed via Flatpak package sandboxes
    QString flatpakPath = QString("/run/user/%1/app/com.discordapp.Discord/discord-ipc-0").arg(uidStr);
    if (QFileInfo::exists(flatpakPath)) return flatpakPath;

    return QString();
}

void DiscordRPC::onConnected() {
    qDebug() << "Connected to Discord IPC Socket. Sending authorization handshake...";
    
    QJsonObject handshake;
    handshake["v"] = 1;
    handshake["client_id"] = m_clientId;

    sendPacket(0, handshake); // OP 0 = Handshake
}

void DiscordRPC::onReadyRead() {
    if (!m_handshakeComplete) {
        m_socket->readAll();
        m_handshakeComplete = true;
        qDebug() << "Discord Handshake complete! Pushing initial music frame...";
        
        if (!m_pendingTrack.isEmpty()) {
            updatePresence(m_pendingTrack, m_pendingArtist, m_pendingAlbumArt, m_playerPositionMs, m_playerDurationMs);
        }
    } else {
        m_socket->readAll();
    }
}

void DiscordRPC::sendPacket(quint32 opCode, const QJsonObject& jsonPayload) {
    if (!m_socket || m_socket->state() != QLocalSocket::ConnectedState) return;

    QByteArray jsonBytes = QJsonDocument(jsonPayload).toJson(QJsonDocument::Compact);
    quint32 length = jsonBytes.length();

    QByteArray headerBytes;
    headerBytes.resize(8);
    memcpy(headerBytes.data(), &opCode, 4);
    memcpy(headerBytes.data() + 4, &length, 4);

    m_socket->write(headerBytes);
    m_socket->write(jsonBytes);
    m_socket->flush();
}

void DiscordRPC::updatePresence(const QString& trackTitle, const QString& artistName, const QString& albumArtUrl, qint64 positionMs, qint64 durationMs) {
    if (!m_handshakeComplete) {
        m_pendingTrack = trackTitle;
        m_pendingArtist = artistName;
        m_pendingAlbumArt = albumArtUrl;
        m_playerPositionMs = positionMs;
        m_playerDurationMs = durationMs;
        return;
    }

    QJsonObject activity;
    activity["type"] = 2; // Type 2 maps to "Listening to..." natively on Discord
    activity["details"] = trackTitle;
    activity["state"] = artistName;
    
    // CRUCIAL: Must be passed as a lower-case string over raw RPC sockets!
    activity["status_display_type"] = 1; 

    QJsonObject assets;
    if (!albumArtUrl.isEmpty()) {
        assets["large_image"] = albumArtUrl;
    } else {
        // Fallback to your developer portal asset key if no URL is available
        assets["large_image"] = QString("app_logo"); 
    }

    activity["assets"] = assets;

    QJsonObject timestamps;
    
    qint64 currentEpochMs = QDateTime::currentMSecsSinceEpoch();

    qint64 startTimeMs = currentEpochMs - positionMs;
    timestamps["start"] = startTimeMs;

    if (durationMs > 0) {
        qint64 endTimeMs = startTimeMs + durationMs;
        timestamps["end"] = endTimeMs;
    }

    activity["timestamps"] = timestamps;

    QJsonObject args;
    args["pid"] = static_cast<int>(QCoreApplication::applicationPid());
    args["activity"] = activity;

    QJsonObject rootPayload;
    rootPayload["cmd"] = QString("SET_ACTIVITY");
    rootPayload["args"] = args;
    rootPayload["nonce"] = QString("1");

    sendPacket(1, rootPayload); // OP 1 = Frame Command Dispatch
}

void DiscordRPC::clearPresence() {
    if (!m_handshakeComplete) return;

    QJsonObject args;
    args["pid"] = static_cast<int>(QCoreApplication::applicationPid());
    args["activity"] = QJsonValue::Null;

    QJsonObject rootPayload;
    rootPayload["cmd"] = QString("SET_ACTIVITY");
    rootPayload["args"] = args;
    rootPayload["nonce"] = QString("1");

    sendPacket(1, rootPayload);
}

void DiscordRPC::onDisconnected() {
    qWarning() << "Discord IPC socket connection dropped.";
    m_handshakeComplete = false;
}