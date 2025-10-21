#include "settingsdialog.h"
#include "ui_settingsdialog.h"

#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QDir>
#include <QProcess>
#include <QDebug>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent),
      ui(new Ui::SettingsDialog)
{
    ui->setupUi(this);

    // sensible defaults (if user hasn't loaded settings)
    ui->editCanID->setText("1803D028");
    ui->editBitrate->setText("250000");
    ui->editForward->setText("");   // user may fill
    ui->editBackward->setText("");
    ui->editLeft->setText("");
    ui->editRight->setText("");
    ui->editStop->setText("");

    // connect OK / Cancel
    connect(ui->buttonBoxOk, &QPushButton::clicked, this, &SettingsDialog::onOkClicked);
    connect(ui->buttonBoxCancel, &QPushButton::clicked, this, &SettingsDialog::reject);
}

SettingsDialog::~SettingsDialog()
{
    delete ui;
}

void SettingsDialog::onOkClicked()
{
    // Save JSON to config path
    QJsonObject obj = toJson();
    QString cfgDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (cfgDir.isEmpty()) cfgDir = ".";
    QDir().mkpath(cfgDir);
    QString cfgFile = cfgDir + "/settings.json";

    QFile f(cfgFile);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QJsonDocument doc(obj);
        f.write(doc.toJson(QJsonDocument::Indented));
        f.close();
    } else {
        qWarning() << "Failed to write settings.json to" << cfgFile;
    }

    // Apply bitrate to can0 via system commands: down -> set type can bitrate X -> up
    // Note: these commands use sudo and may prompt for password; for headless GUI usage configure sudoers appropriately.
    int br = bitrate();
    QString cmdDown = QString("sudo ip link set can0 down");
    QString cmdSet  = QString("sudo ip link set can0 type can bitrate %1").arg(br);
    QString cmdUp   = QString("sudo ip link set can0 up");

    int rcDown = QProcess::execute("sh", QStringList() << "-c" << cmdDown);
    int rcSet  = QProcess::execute("sh", QStringList() << "-c" << cmdSet);
    int rcUp   = QProcess::execute("sh", QStringList() << "-c" << cmdUp);

    if (rcDown != 0 || rcSet != 0 || rcUp != 0) {
        qWarning() << "One or more bitrate commands failed:" << rcDown << rcSet << rcUp;
        // We still accept the dialog so settings are saved; caller (mainwindow) will show warnings if desired.
    }

    accept();
}

uint32_t SettingsDialog::canID() const
{
    bool ok=false;
    QString s = ui->editCanID->text().trimmed();
    if (s.startsWith("0x") || s.startsWith("0X")) s = s.mid(2);
    uint32_t id = s.toUInt(&ok, 16);
    if (!ok) return 0x1803D028u;
    return id;
}

int SettingsDialog::bitrate() const
{
    bool ok=false;
    int b = ui->editBitrate->text().trimmed().toInt(&ok);
    if (!ok) return 250000;
    return b;
}

QByteArray SettingsDialog::parseHexString(const QString &s) const
{
    QString t = s;
    t.remove(' ');
    if (t.size() % 2 != 0) t.prepend('0');
    return QByteArray::fromHex(t.toUtf8());
}

QByteArray SettingsDialog::forwardData() const { return parseHexString(ui->editForward->text()); }
QByteArray SettingsDialog::backwardData() const { return parseHexString(ui->editBackward->text()); }
QByteArray SettingsDialog::leftData() const { return parseHexString(ui->editLeft->text()); }
QByteArray SettingsDialog::rightData() const { return parseHexString(ui->editRight->text()); }
QByteArray SettingsDialog::stopData() const { return parseHexString(ui->editStop->text()); }

void SettingsDialog::loadFromJson(const QJsonObject &obj)
{
    if (obj.contains("can_id")) ui->editCanID->setText(obj["can_id"].toString());
    if (obj.contains("bitrate")) ui->editBitrate->setText(QString::number(obj["bitrate"].toInt()));
    if (obj.contains("forward")) ui->editForward->setText(obj["forward"].toString());
    if (obj.contains("backward")) ui->editBackward->setText(obj["backward"].toString());
    if (obj.contains("left")) ui->editLeft->setText(obj["left"].toString());
    if (obj.contains("right")) ui->editRight->setText(obj["right"].toString());
    if (obj.contains("stop")) ui->editStop->setText(obj["stop"].toString());
}

QJsonObject SettingsDialog::toJson() const
{
    QJsonObject obj;
    obj["can_id"]   = ui->editCanID->text();
    obj["bitrate"]  = ui->editBitrate->text().toInt();
    obj["forward"]  = ui->editForward->text();
    obj["backward"] = ui->editBackward->text();
    obj["left"]     = ui->editLeft->text();
    obj["right"]    = ui->editRight->text();
    obj["stop"]     = ui->editStop->text();
    return obj;
}
