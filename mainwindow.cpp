#include <QMessageBox>
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "settingsdialog.h"

#include <QProcess>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>
#include <QTableWidgetItem>

#include <sys/types.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cstring>
#include <errno.h>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // table header
    ui->tableLog->setColumnCount(5);
    ui->tableLog->setHorizontalHeaderLabels(QStringList() << "Dir" << "Time" << "CAN ID" << "DLC" << "Data");
    ui->tableLog->horizontalHeader()->setStretchLastSection(true);

    // connect UI signals
    connect(ui->btnConnect, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(ui->btnSettings, &QPushButton::clicked, this, &MainWindow::onSettingsClicked);
    connect(ui->btnForward, &QPushButton::clicked, this, &MainWindow::onForwardClicked);
    connect(ui->btnBackward, &QPushButton::clicked, this, &MainWindow::onBackwardClicked);
    connect(ui->btnLeft, &QPushButton::clicked, this, &MainWindow::onLeftClicked);
    connect(ui->btnRight, &QPushButton::clicked, this, &MainWindow::onRightClicked);
    connect(ui->btnStop, &QPushButton::clicked, this, &MainWindow::onStopClicked);
    connect(ui->btnClearLog, &QPushButton::clicked, this, &MainWindow::onClearLogClicked);

    // loop timer
    m_loopTimer.setSingleShot(false);
    connect(&m_loopTimer, &QTimer::timeout, this, &MainWindow::onLoopTimeout);

    // load settings
    loadSettings();
    applySettingsFromJson();

    // initial CAN indicator by checking system state
    updateCanIndicator(isCanInterfaceUp());
}

MainWindow::~MainWindow()
{
    saveSettings();
    closeCanSocket();
    delete ui;
}

// ------------------------- System control helpers -------------------------

bool MainWindow::isCanInterfaceUp() const
{
    QProcess p;
    p.start("ip", QStringList() << "link" << "show" << m_canInterface);
    p.waitForFinished(1000);
    QString out = p.readAllStandardOutput() + p.readAllStandardError();
    // look for "state UP" or "UP"
    return out.contains("state UP") || out.contains("UP");
}

bool MainWindow::bringCanUp()
{
    // sudo ip link set can0 up
    QString cmd = QString("sudo ip link set %1 up").arg(m_canInterface);
    int rc = QProcess::execute("sh", QStringList() << "-c" << cmd);
    qDebug() << "bringCanUp rc=" << rc << "cmd=" << cmd;
    return rc == 0;
}

bool MainWindow::bringCanDown()
{
    QString cmd = QString("sudo ip link set %1 down").arg(m_canInterface);
    int rc = QProcess::execute("sh", QStringList() << "-c" << cmd);
    qDebug() << "bringCanDown rc=" << rc << "cmd=" << cmd;
    return rc == 0;
}

bool MainWindow::setCanBitrate(int bitrate)
{
    // ip link set can0 type can bitrate <bitrate>
    QString cmd = QString("sudo ip link set %1 type can bitrate %2").arg(m_canInterface).arg(bitrate);
    int rc = QProcess::execute("sh", QStringList() << "-c" << cmd);
    qDebug() << "setCanBitrate rc=" << rc << "cmd=" << cmd;
    return rc == 0;
}

// ------------------------- Socket operations -------------------------

bool MainWindow::openCanSocket()
{
    // only open socket when interface is up
    if (!isCanInterfaceUp()) {
        logText("SYS", QString("Interface %1 is DOWN; cannot open socket").arg(m_canInterface));
        return false;
    }

    if (m_socket >= 0) return true;

    m_socket = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (m_socket < 0) {
        logText("SYS", QString("socket() failed: %1").arg(strerror(errno)));
        m_socket = -1;
        return false;
    }

    struct ifreq ifr;
    std::memset(&ifr, 0, sizeof(ifr));
    QByteArray name = m_canInterface.toLocal8Bit();
    std::strncpy(ifr.ifr_name, name.constData(), IFNAMSIZ - 1);

    if (ioctl(m_socket, SIOCGIFINDEX, &ifr) < 0) {
        logText("SYS", QString("ioctl SIOCGIFINDEX failed: %1").arg(strerror(errno)));
        ::close(m_socket); m_socket = -1;
        return false;
    }

    struct sockaddr_can addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(m_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        logText("SYS", QString("bind failed: %1").arg(strerror(errno)));
        ::close(m_socket); m_socket = -1;
        return false;
    }

    // start notifier
    m_notifier = new QSocketNotifier(m_socket, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &MainWindow::onCanReadable);

    logText("SYS", QString("Socket opened on %1").arg(m_canInterface));
    return true;
}

void MainWindow::closeCanSocket()
{
    if (m_notifier) {
        m_notifier->setEnabled(false);
        m_notifier->deleteLater();
        m_notifier = nullptr;
    }
    if (m_socket >= 0) {
        ::close(m_socket);
        m_socket = -1;
    }
    logText("SYS", "Socket closed");
}

void MainWindow::sendCanFrame(const QByteArray &data, bool forceOpen)
{
    if (m_socket < 0) {
        if (!forceOpen) {
            logText("SYS", "Not connected. Cannot send.");
            QMessageBox::warning(this, "Not connected", "CAN interface is not up. Please connect first.");
            return;
        }
        if (!openCanSocket()) {
            QMessageBox::warning(this, "Not connected", "CAN interface not available.");
            return;
        }
    }

    struct can_frame frame;
    std::memset(&frame, 0, sizeof(frame));

    // always send extended 29-bit with CAN_EFF_FLAG
    uint32_t id_val = (m_canId & 0x1FFFFFFFul);
    frame.can_id = id_val | CAN_EFF_FLAG;
    int dlc = qMin(data.size(), 8);
    frame.can_dlc = static_cast<__u8>(dlc);
    if (dlc > 0) std::memcpy(frame.data, data.constData(), dlc);

    ssize_t n = write(m_socket, &frame, sizeof(frame));
    if (n != sizeof(frame)) {
        logText("SYS", QString("write failed: %1").arg(strerror(errno)));
    } else {
        logFrame("TX", frame);
    }
}

// ------------------------- UI slots -------------------------

void MainWindow::onConnectClicked()
{
    bool wasUp = isCanInterfaceUp();
    bool ok = false;
    if (wasUp) {
        ok = bringCanDown();
        if (ok) {
            // close socket if any
            closeCanSocket();
        }
    } else {
        ok = bringCanUp();
    }

    if (!ok) {
        logText("SYS", QString("Failed to toggle interface (sudo may require password)."));
        QMessageBox::warning(this, "Permission / Error", "Failed to toggle can0. Make sure sudo is configured or run program with sufficient privileges.");
    }

    // update indicator after attempting toggle
    updateCanIndicator(isCanInterfaceUp());
}

void MainWindow::onSettingsClicked()
{
    SettingsDialog dlg(this);
    // pass current settings JSON (if any)
    dlg.loadFromJson(m_settingsJson);
    if (dlg.exec() == QDialog::Accepted) {
        // dialog already saved JSON file; refresh
        m_settingsJson = dlg.toJson();

        // apply bitrate: we will run down / set type bitrate / up
        int br = dlg.bitrate();
        // perform commands
        bool downOk = bringCanDown();
        bool setOk = setCanBitrate(br);
        bool upOk = bringCanUp();

        if (!downOk || !setOk || !upOk) {
            logText("SYS", "Failed to set bitrate via system commands (sudo may be required).");
            QMessageBox::warning(this, "Bitrate error", "Setting bitrate failed (needs sudo or polkit). Check logs.");
        } else {
            logText("SYS", QString("Bitrate set to %1").arg(br));
        }

        // save settings and apply
        m_settingsJson = dlg.toJson();
        saveSettings();
        applySettingsFromJson();
    }
}

void MainWindow::onForwardClicked()
{
    QByteArray d = m_forwardData;
    if (ui->chkLoop->isChecked()) {
        // start loop sending forward data
        m_loopMode = true;
        m_loopData = d;
        m_loopIntervalMs = parseIntervalMs(ui->lineInterval->text());
        if (m_loopIntervalMs <= 0) m_loopIntervalMs = 1000;
        if (!m_loopTimer.isActive()) m_loopTimer.start(m_loopIntervalMs);
        // send one immediately
        sendCanFrame(d, true);
    } else {
        sendCanFrame(d, true);
    }
}

void MainWindow::onBackwardClicked()
{
    QByteArray d = m_backwardData;
    if (ui->chkLoop->isChecked()) {
        m_loopMode = true;
        m_loopData = d;
        m_loopIntervalMs = parseIntervalMs(ui->lineInterval->text());
        if (m_loopIntervalMs <= 0) m_loopIntervalMs = 1000;
        if (!m_loopTimer.isActive()) m_loopTimer.start(m_loopIntervalMs);
        sendCanFrame(d, true);
    } else {
        sendCanFrame(d, true);
    }
}

void MainWindow::onLeftClicked()
{
    // send left once (even if loop active)
    sendCanFrame(m_leftData, true);
    // loop continues automatically (we do not stop the loop)
}

void MainWindow::onRightClicked()
{
    sendCanFrame(m_rightData, true);
}

void MainWindow::onStopClicked()
{
    // send stop frame once
    sendCanFrame(m_stopData, true);
    // stop loop
    if (m_loopTimer.isActive()) m_loopTimer.stop();
    m_loopMode = false;
    m_loopData.clear();
}

void MainWindow::onClearLogClicked()
{
    ui->tableLog->setRowCount(0);
}

void MainWindow::onLoopTimeout()
{
    if (m_loopMode && !m_loopData.isEmpty()) {
        sendCanFrame(m_loopData, true);
    } else {
        if (m_loopTimer.isActive()) m_loopTimer.stop();
    }
}

void MainWindow::onCanReadable()
{
    struct can_frame frame;
    ssize_t n = read(m_socket, &frame, sizeof(frame));
    if (n <= 0) {
        logText("SYS", QString("CAN read error: %1").arg(strerror(errno)));
        return;
    }
    logFrame("RX", frame);
}

// ------------------------- logging helpers -------------------------

void MainWindow::logText(const QString &dir, const QString &text)
{
    int r = ui->tableLog->rowCount();
    ui->tableLog->insertRow(r);
    ui->tableLog->setItem(r, 0, new QTableWidgetItem(dir));
    ui->tableLog->setItem(r, 1, new QTableWidgetItem(QDateTime::currentDateTime().toString("HH:mm:ss.zzz")));
    ui->tableLog->setItem(r, 2, new QTableWidgetItem("-"));
    ui->tableLog->setItem(r, 3, new QTableWidgetItem("-"));
    ui->tableLog->setItem(r, 4, new QTableWidgetItem(text));
    ui->tableLog->scrollToBottom();
}

void MainWindow::logFrame(const QString &dir, const struct can_frame &frame)
{
    int r = ui->tableLog->rowCount();
    ui->tableLog->insertRow(r);
    QString idStr = QString::asprintf("0x%08X", frame.can_id & CAN_EFF_MASK);
    QString dataStr;
    for (int i = 0; i < frame.can_dlc; ++i) dataStr += QString::asprintf("%02X ", frame.data[i]);
    dataStr = dataStr.trimmed();
    ui->tableLog->setItem(r, 0, new QTableWidgetItem(dir));
    ui->tableLog->setItem(r, 1, new QTableWidgetItem(QDateTime::currentDateTime().toString("HH:mm:ss.zzz")));
    ui->tableLog->setItem(r, 2, new QTableWidgetItem(idStr));
    ui->tableLog->setItem(r, 3, new QTableWidgetItem(QString::number(frame.can_dlc)));
    ui->tableLog->setItem(r, 4, new QTableWidgetItem(dataStr));
    ui->tableLog->scrollToBottom();
}

void MainWindow::updateCanIndicator(bool up)
{
    QPalette pal = ui->lblCanStatus->palette();
    pal.setColor(QPalette::Window, up ? Qt::green : Qt::red);
    ui->lblCanStatus->setAutoFillBackground(true);
    ui->lblCanStatus->setPalette(pal);
    ui->lblStatusText->setText(up ? "Connected" : "Disconnected");
}

// ------------------------- settings persistence -------------------------

void MainWindow::applySettingsFromJson()
{
    // default values if not present
    if (m_settingsJson.contains("can_id")) {
        QString s = m_settingsJson.value("can_id").toString();
        if (s.startsWith("0x") || s.startsWith("0X")) s = s.mid(2);
        bool ok=false;
        uint32_t id = s.toUInt(&ok, 16);
        if (ok) m_canId = id;
    }
    if (m_settingsJson.contains("forward")) m_forwardData = QByteArray::fromHex(m_settingsJson.value("forward").toString().toUtf8());
    if (m_settingsJson.contains("backward")) m_backwardData = QByteArray::fromHex(m_settingsJson.value("backward").toString().toUtf8());
    if (m_settingsJson.contains("left")) m_leftData = QByteArray::fromHex(m_settingsJson.value("left").toString().toUtf8());
    if (m_settingsJson.contains("right")) m_rightData = QByteArray::fromHex(m_settingsJson.value("right").toString().toUtf8());
    if (m_settingsJson.contains("stop")) m_stopData = QByteArray::fromHex(m_settingsJson.value("stop").toString().toUtf8());
    // bitrate may be present but we don't need to apply here except showing as hint in interval placeholder
    if (m_settingsJson.contains("bitrate")) {
        ui->lineInterval->setPlaceholderText(QString::number(m_settingsJson.value("bitrate").toInt()));
    }
}

void MainWindow::saveSettings()
{
    QString cfgDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (cfgDir.isEmpty()) cfgDir = ".";
    QDir().mkpath(cfgDir);
    QString cfgFile = cfgDir + "/settings.json";

    QFile f(cfgFile);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    QJsonDocument doc(m_settingsJson);
    f.write(doc.toJson(QJsonDocument::Indented));
    f.close();
}

void MainWindow::loadSettings()
{
    QString cfgDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QString cfgFile = cfgDir.isEmpty() ? QString("settings.json") : cfgDir + "/settings.json";
    QFile f(cfgFile);
    if (!f.open(QIODevice::ReadOnly)) {
        // fallback to cwd settings.json
        QFile f2("settings.json");
        if (f2.open(QIODevice::ReadOnly)) {
            QByteArray b = f2.readAll();
            QJsonDocument doc = QJsonDocument::fromJson(b);
            if (doc.isObject()) m_settingsJson = doc.object();
            f2.close();
        }
        return;
    }
    QByteArray b = f.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(b);
    if (doc.isObject()) m_settingsJson = doc.object();
    f.close();
}

// ------------------------- utility -------------------------

int MainWindow::parseIntervalMs(const QString &s) const
{
    QString t = s.trimmed().toLower();
    if (t.isEmpty()) return -1;
    QRegExp re("^(\\d+(?:\\.\\d+)?)(ms|s)?$");
    if (!re.exactMatch(t)) return -1;
    double val = re.cap(1).toDouble();
    QString unit = re.cap(2);
    if (unit == "s") return static_cast<int>(val * 1000.0);
    if (unit == "ms" || unit.isEmpty()) return static_cast<int>(val);
    return static_cast<int>(val);
}
