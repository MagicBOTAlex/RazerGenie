// Copyright (C) 2024  RazerGenie contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef REMAPENGINE_H
#define REMAPENGINE_H

#include <QObject>
#include <QString>

struct input_event;

class QSocketNotifier;
class QTimer;

// Userspace input remapping engine.
//
// When enabled it grabs the mouse's main evdev node (via EVIOCGRAB), clones it
// as a uinput device and forwards all events through the clone. While the
// configured "hyper" button is held, the left/right mouse buttons are turned
// into click-turbos (auto-fire) instead of being passed through unchanged.
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
    void onTurboTick();
    void stopTurbo();

    void passEvent(const input_event &ev);
    void emitEvent(unsigned short type, unsigned short code, int value);
    void sync();

    void setStatus(const QString &status);

    QString m_deviceName;
    bool m_enabled = false;
    int m_hyperButton = 0x113; // BTN_SIDE
    int m_turboRateHz = 15;

    int m_fd = -1;
    int m_ufd = -1;
    QSocketNotifier *m_notifier = nullptr;
    QTimer *m_turboTimer = nullptr;

    bool m_hyperDown = false;
    bool m_physLeft = false;
    bool m_physRight = false;
    int m_turboButton = 0;
    bool m_turboPhaseDown = false;

    QString m_status;
};

#endif // REMAPENGINE_H
