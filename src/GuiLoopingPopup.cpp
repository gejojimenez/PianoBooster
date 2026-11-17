/*!
    @file           GuiLoopingPopup.cpp

    @brief          xxx.

    @author         L. J. Barman

    Copyright (c)   2008-2013, L. J. Barman, all rights reserved

    This file is part of the PianoBooster application

    PianoBooster is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    PianoBooster is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with PianoBooster.  If not, see <http://www.gnu.org/licenses/>.

*/

#include <QtWidgets>

#include <cmath>

#include "GuiLoopingPopup.h"
#include "GlView.h"

GuiLoopingPopup::GuiLoopingPopup(QWidget *parent)
    : QWidget(parent)
{
    setupUi(this);
    m_song = nullptr;
    setWindowTitle(tr("Start and Repeat"));
    setWindowFlags(Qt::Popup);
}

void GuiLoopingPopup::init(CSong* song)
{
    m_song = song;

    showStart(m_song->getPlayFrom());

    const QSignalBlocker blockCheck(loopCheck);
    loopCheck->setChecked(m_song->isLoopEnabled());

    m_end = m_song->getPlayUpto();
    if (!m_song->isLoopEnabled() && !(m_song->getPlayFrom() < m_end))
        m_end = { m_song->getPlayFrom().bar + 1, 0, 0 }; // suggest the bar after the start
    showEnd(m_end);
    updateInfo();
}

// A position as text for the user, e.g. "bar 12" or "bar 12, beat 2.5". The beat is
// rounded down for a start and up for an end, like in the fields.
QString GuiLoopingPopup::positionText(CSong *song, const CBarPosition &position, bool roundUp)
{
    const double exact = (song->beatOfPosition(position) + 1.0) * 100.0;
    const double beat = (roundUp ? std::ceil(exact - 1e-6) : std::floor(exact + 1e-6)) / 100.0;
    if (beat == 1.0)
        return tr("bar %1").arg(position.bar + FIRST_BAR_NUMBER);
    return tr("bar %1, beat %2").arg(position.bar + FIRST_BAR_NUMBER).arg(QLocale().toString(beat, 'g', 4));
}

// Shows a position in the start bar and beat fields, without changing the start.
void GuiLoopingPopup::showStart(const CBarPosition &start)
{
    // rounded down so that typing the shown value again also starts before the notes
    const double beat = std::floor((m_song->beatOfPosition(start) + 1.0) * 100.0 + 1e-6) / 100.0;
    const QSignalBlocker blockBar(startBarSpin);
    const QSignalBlocker blockBeat(startBeatSpin);
    startBarSpin->setValue(start.bar + FIRST_BAR_NUMBER);
    startBeatSpin->setValue(beat);
}

void GuiLoopingPopup::on_startBarSpin_valueChanged(int bar)
{
    Q_UNUSED(bar)
    startEdited();
}

void GuiLoopingPopup::on_startBeatSpin_valueChanged(double beat)
{
    Q_UNUSED(beat)
    startEdited();
}

// The user has typed a start bar or beat
void GuiLoopingPopup::startEdited()
{
    if (!m_song) return;
    // the beat is shown counting from 1; round down so the start is never after the notes
    m_song->setPlayFrom(m_song->positionFromBarBeat(startBarSpin->value() - FIRST_BAR_NUMBER, startBeatSpin->value() - 1.0));
    updateInfo();
}

// Use the exact position of the current notes as the start, so that playing starts on them
void GuiLoopingPopup::on_startHereButton_clicked(bool clicked)
{
    Q_UNUSED(clicked)
    if (!m_song) return;
    const CBarPosition start = m_song->getCurrentNotePosition();
    showStart(start);
    m_song->setPlayFrom(start);
    updateInfo();
}

// Shows a position in the end bar and beat fields, without changing the end.
void GuiLoopingPopup::showEnd(const CBarPosition &end)
{
    // The beat is shown counting from 1, rounded up so that typing the shown value
    // again still ends after the notes.
    const double beat = std::ceil((m_song->beatOfPosition(end) + 1.0) * 100.0 - 1e-6) / 100.0;
    const QSignalBlocker blockBar(endBarSpin);
    const QSignalBlocker blockBeat(endBeatSpin);
    endBarSpin->setValue(end.bar + FIRST_BAR_NUMBER);
    endBeatSpin->setValue(beat);
}

void GuiLoopingPopup::on_loopCheck_toggled(bool checked)
{
    Q_UNUSED(checked)
    applyLoop();
}

void GuiLoopingPopup::on_endBarSpin_valueChanged(int bar)
{
    Q_UNUSED(bar)
    endEdited();
}

void GuiLoopingPopup::on_endBeatSpin_valueChanged(double beat)
{
    Q_UNUSED(beat)
    endEdited();
}

// The user has typed an end bar or beat
void GuiLoopingPopup::endEdited()
{
    if (!m_song) return;
    // the beat is shown counting from 1; round up so the notes at the end are included
    m_end = m_song->positionFromBarBeat(endBarSpin->value() - FIRST_BAR_NUMBER, endBeatSpin->value() - 1.0, true);
    const QSignalBlocker blockCheck(loopCheck);
    loopCheck->setChecked(true); // editing the end means the user wants to repeat
    applyLoop();
}

// Use the exact position of the current notes as the end
void GuiLoopingPopup::on_endHereButton_clicked(bool clicked)
{
    Q_UNUSED(clicked)
    if (!m_song) return;
    m_end = m_song->getCurrentNotePosition();
    showEnd(m_end);
    const QSignalBlocker blockCheck(loopCheck);
    loopCheck->setChecked(true);
    applyLoop();
}

void GuiLoopingPopup::applyLoop()
{
    if (!m_song) return;
    m_song->setLoop(loopCheck->isChecked(), m_end);
    updateInfo();
}

void GuiLoopingPopup::updateInfo()
{
    if (!loopCheck->isChecked())
        loopingText->setText(tr("Repeat Bar is disabled"));
    else if (!m_song->isLoopActive())
        loopingText->setText(tr("The end must be after the start bar"));
    else
        loopingText->setText(tr("Repeats from %1 to %2").arg(positionText(m_song, m_song->getPlayFrom(), false),
                                                            positionText(m_song, m_song->getPlayUpto(), true)));
    emit sectionChanged();
}

void GuiLoopingPopup::closeEvent(QCloseEvent *event)
{
    Q_UNUSED(event)
    auto *parent = qobject_cast<QAbstractButton*>(parentWidget());
    if (parent)
        parent->setChecked(false);
}

