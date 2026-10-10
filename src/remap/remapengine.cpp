// Copyright (C) 2024  RazerGenie contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "remapengine.h"

#include <QCoreApplication>
#include <QDebug>
#include <QSocketNotifier>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/input.h>
#include <linux/uinput.h>

#ifndef BTN_SIDE
#define BTN_SIDE 0x113
#endif
#ifndef BTN_EXTRA
#define BTN_EXTRA 0x114
#endif

namespace {

constexpr unsigned long kLongBits = 8 * sizeof(unsigned long);

bool bitSet(const unsigned long *bits, int bit)
{
    return (bits[bit / kLongBits] >> (bit % kLongBits)) & 1UL;
}

QString normalizeName(QString name)
{
    name.remove(QLatin1String("Razer"));
    name.remove(QLatin1Char(' '));
    return name.toLower();
}

} // namespace

RemapEngine *RemapEngine::instance()
{
    static RemapEngine engine;
    return &engine;
}

RemapEngine::RemapEngine(QObject *parent)
    : QObject(parent)
{
    if (QCoreApplication::instance()) {
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this]() { stop(); });
    }
}

RemapEngine::~RemapEngine()
{
    stop();
}

void RemapEngine::configure(const QString &deviceName, bool enabled, int hyperButton, int turboRateHz)
{
    const bool stateChanged = enabled != m_enabled || deviceName != m_deviceName;

    m_deviceName = deviceName;
    m_enabled = enabled;
    m_hyperButton = hyperButton;
    m_turboRateHz = turboRateHz > 0 ? turboRateHz : 1;
    m_turboRateAtomic.store(m_turboRateHz);

    if (stateChanged) {
        if (m_enabled) {
            start();
        } else {
            stop();
            setStatus(QObject::tr("Disabled"));
        }
        return;
    }

    if (m_enabled && !isRunning()) {
        start();
    }
}

void RemapEngine::setStatus(const QString &status)
{
    if (m_status == status) {
        return;
    }
    m_status = status;
    emit statusChanged();
}

QString RemapEngine::findEventNode() const
{
    const QString want = normalizeName(m_deviceName);
    QString input0;
    QString fallback;

    for (int i = 0; i < 64; i++) {
        const QString node = QStringLiteral("/dev/input/event%1").arg(i);
        const QByteArray path = node.toLocal8Bit();
        int fd = ::open(path.constData(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            continue;
        }

        char name[256] = {0};
        char phys[256] = {0};
        unsigned long relbits[KEY_MAX / kLongBits + 1] = {0};
        ::ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        ::ioctl(fd, EVIOCGPHYS(sizeof(phys)), phys);
        ::ioctl(fd, EVIOCGBIT(EV_REL, sizeof(relbits)), relbits);
        ::close(fd);

        const QString inName = QString::fromLocal8Bit(name);
        const bool isMouse = bitSet(relbits, REL_X);
        const bool nameMatch = !want.isEmpty() && normalizeName(inName).contains(want);

        if (nameMatch && isMouse) {
            if (QString::fromLocal8Bit(phys).endsWith(QLatin1String("/input0"))) {
                input0 = node;
                break;
            }
            if (fallback.isEmpty()) {
                fallback = node;
            }
        }
    }

    return input0.isEmpty() ? fallback : input0;
}

bool RemapEngine::createVirtualDevice(int srcFd)
{
    m_ufd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (m_ufd < 0) {
        setStatus(tr("Cannot open /dev/uinput: %1").arg(QString::fromLocal8Bit(strerror(errno))));
        return false;
    }

    struct input_id id;
    ::memset(&id, 0, sizeof(id));
    ::ioctl(srcFd, EVIOCGID, &id);

    unsigned long evbits[EV_MAX / kLongBits + 1] = {0};
    ::ioctl(srcFd, EVIOCGBIT(0, sizeof(evbits)), evbits);

    ::ioctl(m_ufd, UI_SET_EVBIT, EV_KEY);
    ::ioctl(m_ufd, UI_SET_EVBIT, EV_REL);
    ::ioctl(m_ufd, UI_SET_EVBIT, EV_SYN);

    const int types[] = { EV_KEY, EV_REL, EV_MSC, EV_LED, EV_SW };
    for (int type : types) {
        if (type != EV_KEY && type != EV_REL && !bitSet(evbits, type)) {
            continue;
        }
        unsigned long bits[KEY_MAX / kLongBits + 1] = {0};
        if (::ioctl(srcFd, EVIOCGBIT(type, sizeof(bits)), bits) < 0) {
            continue;
        }
        for (int code = 0; code <= KEY_MAX; code++) {
            if (!bitSet(bits, code)) {
                continue;
            }
            switch (type) {
            case EV_KEY:
                ::ioctl(m_ufd, UI_SET_KEYBIT, code);
                break;
            case EV_REL:
                ::ioctl(m_ufd, UI_SET_RELBIT, code);
                break;
            case EV_MSC:
                ::ioctl(m_ufd, UI_SET_MSCBIT, code);
                break;
            case EV_LED:
                ::ioctl(m_ufd, UI_SET_LEDBIT, code);
                break;
            case EV_SW:
                ::ioctl(m_ufd, UI_SET_SWBIT, code);
                break;
            default:
                break;
            }
        }
    }

    ::ioctl(m_ufd, UI_SET_EVBIT, EV_KEY);
    ::ioctl(m_ufd, UI_SET_KEYBIT, BTN_LEFT);
    ::ioctl(m_ufd, UI_SET_KEYBIT, BTN_RIGHT);
    ::ioctl(m_ufd, UI_SET_KEYBIT, m_hyperButton);

    struct uinput_setup setup;
    ::memset(&setup, 0, sizeof(setup));
    setup.id = id;
    ::strncpy(setup.name, "RazerGenie Virtual Mouse", UINPUT_MAX_NAME_SIZE - 1);

    if (::ioctl(m_ufd, UI_DEV_SETUP, &setup) < 0) {
        setStatus(tr("UI_DEV_SETUP failed: %1").arg(QString::fromLocal8Bit(strerror(errno))));
        ::close(m_ufd);
        m_ufd = -1;
        return false;
    }

    if (::ioctl(m_ufd, UI_DEV_CREATE) < 0) {
        setStatus(tr("UI_DEV_CREATE failed: %1").arg(QString::fromLocal8Bit(strerror(errno))));
        ::close(m_ufd);
        m_ufd = -1;
        return false;
    }

    return true;
}

bool RemapEngine::start()
{
    stop();

    if (m_deviceName.isEmpty()) {
        setStatus(tr("No device selected"));
        return false;
    }

    const QString node = findEventNode();
    if (node.isEmpty()) {
        setStatus(tr("Input device for \"%1\" not found").arg(m_deviceName));
        return false;
    }

    const QByteArray path = node.toLocal8Bit();
    m_fd = ::open(path.constData(), O_RDWR | O_NONBLOCK);
    if (m_fd < 0) {
        m_fd = ::open(path.constData(), O_RDONLY | O_NONBLOCK);
    }
    if (m_fd < 0) {
        setStatus(tr("Cannot open %1: %2").arg(node, QString::fromLocal8Bit(strerror(errno))));
        return false;
    }

    if (::ioctl(m_fd, EVIOCGRAB, 1) < 0) {
        setStatus(tr("Cannot grab %1: %2").arg(node, QString::fromLocal8Bit(strerror(errno))));
        ::close(m_fd);
        m_fd = -1;
        return false;
    }

    if (!createVirtualDevice(m_fd)) {
        ::ioctl(m_fd, EVIOCGRAB, 0);
        ::close(m_fd);
        m_fd = -1;
        return false;
    }

    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &RemapEngine::onActivated);

    m_hyperDown = false;
    m_physLeft = false;
    m_physRight = false;
    m_turboButton = 0;

    setStatus(tr("Active on %1").arg(node));
    return true;
}

void RemapEngine::stop()
{
    stopTurbo();

    if (m_notifier) {
        m_notifier->setEnabled(false);
        delete m_notifier;
        m_notifier = nullptr;
    }
    if (m_ufd >= 0) {
        ::ioctl(m_ufd, UI_DEV_DESTROY);
        ::close(m_ufd);
        m_ufd = -1;
    }
    if (m_fd >= 0) {
        ::ioctl(m_fd, EVIOCGRAB, 0);
        ::close(m_fd);
        m_fd = -1;
    }
    m_hyperDown = false;
    m_physLeft = false;
    m_physRight = false;
}

void RemapEngine::writeEvent(const input_event &ev)
{
    std::lock_guard<std::mutex> lock(m_writeMutex);
    if (m_ufd >= 0) {
        [[maybe_unused]] ssize_t written = ::write(m_ufd, &ev, sizeof(ev));
    }
}

void RemapEngine::passEvent(const input_event &ev)
{
    writeEvent(ev);
}

void RemapEngine::emitEvent(unsigned short type, unsigned short code, int value)
{
    struct input_event ev;
    ::memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.code = code;
    ev.value = value;
    writeEvent(ev);
}

void RemapEngine::sync()
{
    emitEvent(EV_SYN, SYN_REPORT, 0);
}

void RemapEngine::startTurbo(int button)
{
    if (m_turboRun.load() && m_turboCode.load() == button) {
        return;
    }
    stopTurbo();

    m_turboButton = button;
    m_turboCode.store(button);
    m_turboRateAtomic.store(m_turboRateHz);
    m_turboRun.store(true);
    m_turboThread = std::thread(&RemapEngine::turboLoop, this);
}

void RemapEngine::stopTurbo()
{
    m_turboRun.store(false);
    if (m_turboThread.joinable()) {
        m_turboThread.join();
    }
    m_turboButton = 0;
}

void RemapEngine::turboLoop()
{
    const int button = m_turboCode.load();
    while (m_turboRun.load()) {
        const int rate = std::max(1, m_turboRateAtomic.load());
        const int halfUs = std::max(1, 1000000 / rate / 2);

        emitEvent(EV_KEY, button, 1);
        sync();
        std::this_thread::sleep_for(std::chrono::microseconds(halfUs));

        emitEvent(EV_KEY, button, 0);
        sync();
        std::this_thread::sleep_for(std::chrono::microseconds(halfUs));
    }
}

void RemapEngine::onActivated()
{
    if (m_fd < 0) {
        return;
    }

    struct input_event ev;
    while (::read(m_fd, &ev, sizeof(ev)) == static_cast<ssize_t>(sizeof(ev))) {
        if (ev.type == EV_KEY && (ev.code == BTN_LEFT || ev.code == BTN_RIGHT)) {
            const bool down = ev.value != 0;
            if (ev.code == BTN_LEFT) {
                m_physLeft = down;
            } else {
                m_physRight = down;
            }

            if (m_hyperDown) {
                if (down) {
                    startTurbo(ev.code);
                } else if (m_turboButton == ev.code) {
                    stopTurbo();
                }
                continue;
            }

            passEvent(ev);
            continue;
        }

        if (ev.type == EV_KEY && ev.code == m_hyperButton) {
            const bool down = ev.value != 0;
            if (down) {
                m_hyperDown = true;
            } else if (m_hyperDown) {
                m_hyperDown = false;
                const int turbo = m_turboButton;
                if (turbo != 0) {
                    stopTurbo();
                    if ((turbo == BTN_LEFT && m_physLeft) || (turbo == BTN_RIGHT && m_physRight)) {
                        emitEvent(EV_KEY, turbo, 1);
                        sync();
                    }
                }
            }
            passEvent(ev);
            continue;
        }

        passEvent(ev);
    }
}
