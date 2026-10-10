// Copyright (C) 2024  RazerGenie contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef REMAPENGINE_H
#define REMAPENGINE_H

#include <QObject>
#include <QString>

#include <atomic>
#include <mutex>
#include <thread>

struct input_event;

class QSocketNotifier;

class RemapEngine : public QObject
{
    Q_OBJECT
public:
    static RemapEngine *instance();

    void configure(const QString &deviceName, bool enabled, int hyperButton, int turboRateHz);

    bool isRunning() const { return m_fd >= 0; }
    bool isEnabled() const { return m_enabled; }
    QString status() const { return m_status; }

signals:
    void statusChanged();

private:
    explicit RemapEngine(QObject *parent = nullptr);
    ~RemapEngine() override;

    bool start();
    void stop();

    QString findEventNode() const;
    bool createVirtualDevice(int srcFd);

    void onActivated();
    void startTurbo(int button);
    void stopTurbo();
    void turboLoop();

    void writeEvent(const input_event &ev);
    void passEvent(const input_event &ev);
    void emitEvent(unsigned short type, unsigned short code, int value);
    void sync();

    void setStatus(const QString &status);

    QString m_deviceName;
    bool m_enabled = false;
    int m_hyperButton = 0x113;
    int m_turboRateHz = 15;

    int m_fd = -1;
    int m_ufd = -1;
    QSocketNotifier *m_notifier = nullptr;

    bool m_hyperDown = false;
    bool m_physLeft = false;
    bool m_physRight = false;
    int m_turboButton = 0;

    std::thread m_turboThread;
    std::atomic<bool> m_turboRun{ false };
    std::atomic<int> m_turboCode{ 0 };
    std::atomic<int> m_turboRateAtomic{ 15 };
    std::mutex m_writeMutex;

    QString m_status;
};

#endif // REMAPENGINE_H
