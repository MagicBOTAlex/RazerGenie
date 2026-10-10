// Copyright (C) 2024  RazerGenie contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef REMAPWIDGET_H
#define REMAPWIDGET_H

#include <QSettings>
#include <QWidget>
#include <libopenrazer.h>

class QCheckBox;
class QComboBox;
class QLabel;
class QSpinBox;

class RemapWidget : public QWidget
{
    Q_OBJECT
public:
    RemapWidget(libopenrazer::Device *device, QWidget *parent = nullptr);
    ~RemapWidget() override;

    static bool isAvailable(libopenrazer::Device *device);

private:
    void apply();
    void updateStatus();

    libopenrazer::Device *device = nullptr;
    QSettings settings;

    QCheckBox *enableBox = nullptr;
    QComboBox *hyperCombo = nullptr;
    QSpinBox *rateSpin = nullptr;
    QLabel *statusLabel = nullptr;
};

#endif // REMAPWIDGET_H
