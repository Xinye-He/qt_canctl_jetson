#pragma once
#include <QDialog>
#include <QByteArray>
#include <QJsonObject>

namespace Ui { class SettingsDialog; }

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog();

    uint32_t canID() const;
    int bitrate() const;
    QByteArray forwardData() const;
    QByteArray backwardData() const;
    QByteArray leftData() const;
    QByteArray rightData() const;
    QByteArray stopData() const;

    void loadFromJson(const QJsonObject &obj);
    QJsonObject toJson() const;

private slots:
    void onOkClicked();

private:
    Ui::SettingsDialog *ui;
    QByteArray parseHexString(const QString &s) const;
};
