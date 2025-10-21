#include "canmanager.h"
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <errno.h>
#include <QDebug>

CanManager::CanManager(QObject *parent)
    : QObject(parent), socket_fd(-1), notifier(nullptr)
{
}

CanManager::~CanManager()
{
    close();
}

bool CanManager::open(const std::string &ifname)
{
    QMutexLocker locker(&mtx);
    if (socket_fd >= 0) return true;

    socket_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (socket_fd < 0) {
        emit canStatusChanged(false);
        return false;
    }

    struct ifreq ifr;
    std::strncpy(ifr.ifr_name, ifname.c_str(), IFNAMSIZ-1);
    ifr.ifr_name[IFNAMSIZ-1] = '\0';

    if (ioctl(socket_fd, SIOCGIFINDEX, &ifr) < 0) {
        ::close(socket_fd);
        socket_fd = -1;
        emit canStatusChanged(false);
        return false;
    }

    struct sockaddr_can addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(socket_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ::close(socket_fd);
        socket_fd = -1;
        emit canStatusChanged(false);
        return false;
    }

    notifier = new QSocketNotifier(socket_fd, QSocketNotifier::Read, this);
    connect(notifier, &QSocketNotifier::activated, this, &CanManager::onCanReadable);

    emit canStatusChanged(true);
    return true;
}

void CanManager::close()
{
    QMutexLocker locker(&mtx);
    if (notifier) {
        notifier->setEnabled(false);
        notifier->deleteLater();
        notifier = nullptr;
    }
    if (socket_fd >= 0) {
        ::close(socket_fd);
        socket_fd = -1;
    }
    emit canStatusChanged(false);
}

bool CanManager::isOpen() const { return socket_fd >= 0; }

bool CanManager::sendFrame(uint32_t can_id, const QByteArray &data)
{
    QMutexLocker locker(&mtx);
    if (socket_fd < 0) return false;

    struct can_frame frame;
    std::memset(&frame, 0, sizeof(frame));
    frame.can_id = (can_id & 0x1FFFFFFFul) | CAN_EFF_FLAG;
    int dlc = qMin(data.size(), 8);
    frame.can_dlc = dlc;
    for (int i=0;i<dlc;i++) frame.data[i] = static_cast<uint8_t>(data[i]);

    ssize_t n = write(socket_fd, &frame, sizeof(frame));
    if (n != sizeof(frame)) {
        qWarning() << "CAN write failed:" << strerror(errno);
        return false;
    }
    emit frameSent(can_id, data, QDateTime::currentDateTime());
    return true;
}

void CanManager::onCanReadable()
{
    struct can_frame frame;
    ssize_t n = read(socket_fd, &frame, sizeof(frame));
    if (n != sizeof(frame)) return;
    QByteArray data(reinterpret_cast<const char *>(frame.data), frame.can_dlc);
    uint32_t id = frame.can_id & CAN_EFF_MASK;
    emit frameReceived(id, data, QDateTime::currentDateTime());
}
