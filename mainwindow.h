#pragma once

#include <QMainWindow>
#include <QTimer>
#include <QSocketNotifier>
#include <QJsonObject>

namespace Ui { class MainWindow; }

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // UI
    void onConnectClicked();
    void onSettingsClicked();
    void onForwardClicked();
    void onBackwardClicked();
    void onLeftClicked();
    void onRightClicked();
    void onStopClicked();
    void onClearLogClicked();

    // loop
    void onLoopTimeout();

    // socket read
    void onCanReadable();

private:
    // helpers
    bool isCanInterfaceUp() const;
    bool bringCanUp();
    bool bringCanDown();
    bool setCanBitrate(int bitrate);
    bool openCanSocket();           // open socket if interface is UP
    void closeCanSocket();
    void sendCanFrame(const QByteArray &data, bool forceOpen = false);
    int parseIntervalMs(const QString &s) const;

    void logText(const QString &dir, const QString &text);
    void logFrame(const QString &dir, const struct can_frame &frame);
    void updateCanIndicator(bool up);

    // settings
    void loadSettings();
    void saveSettings();
    void applySettingsFromJson();

private:
    Ui::MainWindow *ui;

    // socketCAN
    int m_socket = -1;
    QSocketNotifier *m_notifier = nullptr;

    // loop timer
    QTimer m_loopTimer;
    QByteArray m_loopData;   // data being loop-sent
    bool m_loopMode = false;
    int m_loopIntervalMs = 1000;

    // config
    QJsonObject m_settingsJson;
    QString m_canInterface = QStringLiteral("can0");
    uint32_t m_canId = 0x1803D028;
    QByteArray m_forwardData;
    QByteArray m_backwardData;
    QByteArray m_leftData;
    QByteArray m_rightData;
    QByteArray m_stopData;
};
