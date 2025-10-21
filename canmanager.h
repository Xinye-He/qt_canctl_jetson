#pragma once
#include <QObject>
#include <QByteArray>
#include <QDateTime>
#include <QMutex>
#include <QSocketNotifier>

class CanManager : public QObject
{
    Q_OBJECT
public:
    explicit CanManager(QObject *parent = nullptr);
    ~CanManager();

    bool open(const std::string &ifname = "can0");
    void close();
    bool isOpen() const;

    bool sendFrame(uint32_t can_id, const QByteArray &data);

signals:
    void canStatusChanged(bool ok);
    void frameSent(uint32_t id, const QByteArray &data, const QDateTime &ts);
    void frameReceived(uint32_t id, const QByteArray &data, const QDateTime &ts);

private slots:
    void onCanReadable();

private:
    int socket_fd = -1;
    QMutex mtx;
    QSocketNotifier *notifier = nullptr;
};
