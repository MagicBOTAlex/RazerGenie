// Copyright (C) 2024  RazerGenie contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "remapwidget.h"
#include "../remap/remapengine.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpacerItem>
#include <QSpinBox>
#include <QVBoxLayout>

#include <linux/input.h>

#ifndef BTN_SIDE
#define BTN_SIDE 0x113
#endif
#ifndef BTN_EXTRA
#define BTN_EXTRA 0x114
#endif

RemapWidget::RemapWidget(libopenrazer::Device *device, QWidget *parent)
    : QWidget(parent), device(device)
{
    auto *verticalLayout = new QVBoxLayout(this);

    QFont headerFont("Arial", 15, QFont::Bold);

    QLabel *header = new QLabel(tr("HyperShift remapping"), this);
    header->setFont(headerFont);
    verticalLayout->addWidget(header);

    QLabel *description = new QLabel(
        tr("While the selected Hyper button is held down, holding the left or right mouse "
           "button will auto-fire (turbo) that button until it is released. This runs in the "
           "background for as long as RazerGenie is running."),
        this);
    description->setWordWrap(true);
    verticalLayout->addWidget(description);

    enableBox = new QCheckBox(tr("Enable HyperShift remapping"), this);
    verticalLayout->addWidget(enableBox);

    auto *formLayout = new QFormLayout();

    hyperCombo = new QComboBox(this);
    hyperCombo->addItem(tr("Side button"), static_cast<int>(BTN_SIDE));
    hyperCombo->addItem(tr("Extra button"), static_cast<int>(BTN_EXTRA));
    hyperCombo->addItem(tr("Back button"), static_cast<int>(BTN_BACK));
    hyperCombo->addItem(tr("Forward button"), static_cast<int>(BTN_FORWARD));
    hyperCombo->addItem(tr("Middle button"), static_cast<int>(BTN_MIDDLE));
    formLayout->addRow(tr("Hyper button:"), hyperCombo);

    rateSpin = new QSpinBox(this);
    rateSpin->setRange(1, 50);
    rateSpin->setSuffix(tr(" /s"));
    formLayout->addRow(tr("Turbo rate:"), rateSpin);

    verticalLayout->addLayout(formLayout);

    statusLabel = new QLabel(this);
    statusLabel->setWordWrap(true);
    verticalLayout->addWidget(statusLabel);

    auto *spacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);
    verticalLayout->addItem(spacer);

    enableBox->setChecked(settings.value("remap/enabled", false).toBool());
    const int hyperButton = settings.value("remap/hyperButton", static_cast<int>(BTN_SIDE)).toInt();
    const int comboIndex = hyperCombo->findData(hyperButton);
    if (comboIndex >= 0) {
        hyperCombo->setCurrentIndex(comboIndex);
    }
    rateSpin->setValue(settings.value("remap/rateHz", 15).toInt());

    connect(enableBox, &QCheckBox::toggled, this, [this]() { apply(); });
    connect(hyperCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { apply(); });
    connect(rateSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { apply(); });
    connect(RemapEngine::instance(), &RemapEngine::statusChanged, this, [this]() { updateStatus(); });

    apply();
}

RemapWidget::~RemapWidget() = default;

bool RemapWidget::isAvailable(libopenrazer::Device *device)
{
    return device->getDeviceType() == "mouse";
}

void RemapWidget::apply()
{
    const bool enabled = enableBox->isChecked();
    const int hyperButton = hyperCombo->currentData().toInt();
    const int rateHz = rateSpin->value();

    settings.setValue("remap/enabled", enabled);
    settings.setValue("remap/hyperButton", hyperButton);
    settings.setValue("remap/rateHz", rateHz);

    hyperCombo->setEnabled(enabled);
    rateSpin->setEnabled(enabled);

    if (device) {
        RemapEngine::instance()->configure(device->getDeviceName(), enabled, hyperButton, rateHz);
    }

    updateStatus();
}

void RemapWidget::updateStatus()
{
    statusLabel->setText(tr("Status: %1").arg(RemapEngine::instance()->status()));
}
