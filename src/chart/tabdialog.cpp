/*
 * Copyright (c) 2007, Aconex.  All Rights Reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */
#include "tabdialog.h"
#include "main.h"

TabDialog::TabDialog(QWidget* parent) : QDialog(parent)
{
    setupUi(this);
}

void TabDialog::languageChange()
{
    retranslateUi(this);
}

void TabDialog::reset(QString label, bool live, int samples, int visible)
{
    labelLineEdit->setText(label);
    if (label == QString::null)
	setWindowTitle(tr("Add Tab"));
    else {
	setWindowTitle(tr("Edit Tab"));
	liveHostRadioButton->setEnabled(false);
	archivesRadioButton->setEnabled(false);
    }

    liveHostRadioButton->setChecked(live);
    archivesRadioButton->setChecked(!live);

    my.archiveSource = !live;
    my.samples = my.visible = 0;

    visibleCounter->setValue(visible);
    visibleCounter->setRange(KmChart::minimumPoints(), KmChart::maximumPoints());
    visibleSlider->setValue(visible);
    visibleSlider->setRange(KmChart::minimumPoints(), KmChart::maximumPoints());
    sampleCounter->setValue(samples);
    sampleCounter->setRange(KmChart::minimumPoints(), KmChart::maximumPoints());
    sampleSlider->setValue(samples);
    sampleSlider->setRange(KmChart::minimumPoints(), KmChart::maximumPoints());

    console->post(KmChart::DebugUi, "TabDialog::reset archive=%s",
					my.archiveSource?"true":"false");
}

bool TabDialog::isArchiveSource()
{
    console->post(KmChart::DebugUi, "TabDialog::isArchiveSource archive=%s",
		  my.archiveSource?"true":"false");
    return my.archiveSource;
}

void TabDialog::liveHostRadioButtonClicked()
{
    liveHostRadioButton->setChecked(true);
    archivesRadioButton->setChecked(false);
    my.archiveSource = false;
    console->post(KmChart::DebugUi,
		  "TabDialog::liveHostRadioButtonClicked archive=%s",
		  my.archiveSource?"true":"false");
}

void TabDialog::archivesRadioButtonClicked()
{
    liveHostRadioButton->setChecked(false);
    archivesRadioButton->setChecked(true);
    my.archiveSource = true;
    console->post(KmChart::DebugUi,
		  "TabDialog::archivesRadioButtonClicked archive=%s",
		  my.archiveSource?"true":"false");
}

void TabDialog::sampleValueChanged(int value)
{
    if (my.samples != value) {
	my.samples = value;
	displaySampleCounter();
	displaySampleSlider();
	if (my.visible > my.samples)
	    visibleSlider->setValue(value);
    }
}

void TabDialog::visibleValueChanged(int value)
{
    if (my.visible != value) {
	my.visible = value;
	displayVisibleCounter();
	displayVisibleSlider();
	if (my.visible > my.samples)
	    sampleSlider->setValue(value);
    }
}

void TabDialog::displaySampleSlider()
{
    sampleSlider->blockSignals(true);
    sampleSlider->setValue(my.samples);
    sampleSlider->blockSignals(false);
}

void TabDialog::displayVisibleSlider()
{
    visibleSlider->blockSignals(true);
    visibleSlider->setValue(my.visible);
    visibleSlider->blockSignals(false);
}

void TabDialog::displaySampleCounter()
{
    sampleCounter->blockSignals(true);
    sampleCounter->setValue(my.samples);
    sampleCounter->blockSignals(false);
}

void TabDialog::displayVisibleCounter()
{
    visibleCounter->blockSignals(true);
    visibleCounter->setValue(my.visible);
    visibleCounter->blockSignals(false);
}
