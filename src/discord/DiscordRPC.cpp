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
    
    // Step 1: Execute Protocol Version 1 Handshake
    QJsonObject handshake;
    handshake["v"] = 1;
    handshake["client_id"] = m_clientId;

    sendPacket(0, handshake); // OP 0 = Handshake
}

void DiscordRPC::onReadyRead() {
    // Read response packet headers to verify handshake acknowledgment 
    if (!m_handshakeComplete) {
        m_socket->readAll(); // Clear acknowledgment frame out of stream buffer
        m_handshakeComplete = true;
        qDebug() << "Discord Handshake complete! Pushing initial music frame...";
        
        if (!m_pendingTrack.isEmpty()) {
            updatePresence(m_pendingTrack, m_pendingArtist, m_pendingAlbumArt, m_playerPositionMs, m_playerDurationMs);
        }
    } else {
        m_socket->readAll(); // Discard standard frame response echo logs
    }
}

void DiscordRPC::sendPacket(quint32 opCode, const QJsonObject& jsonPayload) {
    if (!m_socket || m_socket->state() != QLocalSocket::ConnectedState) return;

    QByteArray jsonBytes = QJsonDocument(jsonPayload).toJson(QJsonDocument::Compact);
    quint32 length = jsonBytes.length();

    // Pack headers into Little Endian ordered binary stream segments
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

    // Build the specific Activity Schema used by the JavaScript application
    QJsonObject activity;
    activity["type"] = 2; // Type 2 maps to "Listening to..." natively on Discord
    activity["details"] = trackTitle;
    activity["state"] = artistName;
    
    // CRUCIAL: Must be passed as a lower-case string over raw RPC sockets!
    activity["status_display_type"] = 1; 

    // Optional Visual Assets
    QJsonObject assets;
    if (!albumArtUrl.isEmpty()) {
        // If a valid URL is provided, pass the text string directly
        assets["large_image"] = albumArtUrl;
    } else {
        // Fallback to your developer portal asset key if no URL is available
        assets["large_image"] = QString("app_logo"); 
    }

    activity["assets"] = assets;

    QJsonObject timestamps;
    
    // Get the current local system epoch time in milliseconds
    qint64 currentEpochMs = QDateTime::currentMSecsSinceEpoch();

    // Back-calculate the exact millisecond the song initially clicked "Play"
    qint64 startTimeMs = currentEpochMs - positionMs;
    timestamps["start"] = startTimeMs;

    // Only apply the ending timestamp line if a valid track duration is resolved (> 0)
    if (durationMs > 0) {
        qint64 endTimeMs = startTimeMs + durationMs;
        timestamps["end"] = endTimeMs;
    }

    activity["timestamps"] = timestamps;

    // Wrap the activity structure into an RPC Command Object Payload
    QJsonObject args;
    args["pid"] = static_cast<int>(QCoreApplication::applicationPid());
    args["activity"] = activity;

    QJsonObject rootPayload;
    rootPayload["cmd"] = QString("SET_ACTIVITY"); // Command label identifier
    rootPayload["args"] = args;
    rootPayload["nonce"] = QString("1");

    sendPacket(1, rootPayload); // OP 1 = Frame Command Dispatch
}

void DiscordRPC::clearPresence() {
    if (!m_handshakeComplete) return;

    // Send a SET_ACTIVITY frame with an omitted activity object to wipe it clean
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