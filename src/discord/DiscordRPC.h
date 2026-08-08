#pragma once
#include <QObject>
#include <QLocalSocket>
#include <QJsonObject>
#include <QJsonDocument>

class DiscordRPC : public QObject {
    Q_OBJECT
public:
    explicit DiscordRPC(QObject* parent = nullptr);
    ~DiscordRPC();

    void updatePresence(const QString& trackTitle, const QString& artistName, const QString& albumArtUrl, qint64 positionMs, qint64 durationMs);
    void clearPresence();

private slots:
    void onConnected();
    void onReadyRead();
    void onDisconnected();

private:
    void sendPacket(quint32 opCode, const QJsonObject& jsonPayload);
    QString findDiscordSocketPath();

    QLocalSocket* m_socket{nullptr};
    bool m_handshakeComplete{false};
    QString m_clientId{"1535368938823745616"};
    
    // Cached values in case we need to push updates before the handshake completes
    QString m_pendingTrack;
    QString m_pendingArtist;
    QString m_pendingAlbumArt;
    qint64 m_playerPositionMs{0};
    qint64 m_playerDurationMs{0};
};
