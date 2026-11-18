/*********************************************************************************/
/*!
@file           Bar.cpp

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
/*********************************************************************************/

#include "Bar.h"

#include <cmath>

#define OPTION_DEBUG_BAR     0
#if OPTION_DEBUG_BAR
#define ppDEBUG_BAR(args)     ppLogDebug args
#else
#define ppDEBUG_BAR(args)
#endif

void CBar::setTimeSig(int top, int bottom)
{
    m_currentTimeSigTop = top;
    m_currentTimeSigBottom = bottom;
    if (bottom == 0 || m_startTimeSigBottom == 0) {
        m_startTimeSigTop = top;
        m_startTimeSigBottom = bottom;
        if (bottom == 0 )
        {
            m_currentTimeSigTop = 4;
            m_currentTimeSigBottom = 4;
            m_deltaTime = 0;
            m_beatCounter = 0;
            m_barCounter = 0;
        }
    }
    m_beatLength = (CMidiFile::getPulsesPerQuarterNote() *4)/ m_currentTimeSigBottom;
    m_barLength = m_beatLength * getTimeSigTop();
}

qint64 CBar::addDeltaTime(qint64 ticks)
{
    if (m_flushTicks == true && ticks != 0) // ignore this set of ticks
    {
        ppDEBUG_BAR(("addDeltaTime m_flushTicks ticks %d", ticks));
        m_flushTicks = false;
        return 0;
    }
    m_deltaTime +=ticks;
    normaliseDeltaTime();
    if (ticks && m_enablePlayFromBar)
        checkGotoBar();

    return ticks;
}

void CBar::adjustDeltaTime(qint64 ticks)
{
    m_deltaTime += ticks;
    normaliseDeltaTime();
}

void CBar::addTimeSigChange(qint64 tick, int top, int bottom)
{
    if (top <= 0 || bottom <= 0)
        return;
    CTimeSigSegment segment;
    segment.top = top;
    segment.bottom = bottom;
    segment.beatLength = (CMidiFile::getPulsesPerQuarterNote() * 4) / bottom;
    if (segment.beatLength <= 0)
        return;

    // like rewind(), the first time signature of the song is used from its start
    if (!m_timeSigMap.empty())
    {
        const CTimeSigSegment &last = m_timeSigMap.back();
        const qint64 barTicks = last.beatLength * last.top;
        // the whole bars of the last time signature up to the change, rounded up
        const qint64 bars = (tick > last.startTick) ? (tick - last.startTick + barTicks - 1) / barTicks : 0;
        segment.startBar = last.startBar + static_cast<int>(bars);
        segment.startTick = last.startTick + bars * barTicks;
        if (segment.startBar == last.startBar)
        {
            m_timeSigMap.back() = segment; // replaces a change at the same bar
            return;
        }
        if (last.top == top && last.bottom == bottom)
            return; // nothing changes
    }
    m_timeSigMap.push_back(segment);
}

const CTimeSigSegment *CBar::segmentOfBar(int bar) const
{
    const CTimeSigSegment *found = nullptr;
    for (const CTimeSigSegment &segment : m_timeSigMap)
    {
        if (segment.startBar > bar && found)
            break;
        found = &segment;
    }
    return found;
}

int CBar::beatsInBar(int bar) const
{
    const CTimeSigSegment *segment = segmentOfBar(bar);
    return segment ? segment->top : m_currentTimeSigTop;
}

qint64 CBar::beatTicksInBar(int bar) const
{
    const CTimeSigSegment *segment = segmentOfBar(bar);
    return (segment ? segment->beatLength : m_beatLength) * SPEED_ADJUST_FACTOR;
}

void CBar::normaliseDeltaTime()
{
    // the length of the beats changes with the time signature of each bar
    qint64 beatTicks = beatTicksInBar(m_barCounter);
    if (beatTicks <= 0)
        return;

    while (m_deltaTime >= beatTicks)
    {
        m_deltaTime -= beatTicks;
        m_beatCounter++;
        if (m_beatCounter >= beatsInBar(m_barCounter))
        {
            m_barCounter++;
            m_beatCounter=0;
            ppLogDebug("Bar number %d", m_barCounter);
            m_eventBits |= EVENT_BITS_newBarNumber;
            beatTicks = beatTicksInBar(m_barCounter);
            if (beatTicks <= 0)
                return;
        }
    }
    while (m_deltaTime < 0 && (m_beatCounter > 0 || m_barCounter > 0))
    {
        if (m_beatCounter > 0)
            m_beatCounter--;
        else
        {
            m_barCounter--;
            m_beatCounter = beatsInBar(m_barCounter) - 1;
            m_eventBits |= EVENT_BITS_newBarNumber;
            beatTicks = beatTicksInBar(m_barCounter);
            if (beatTicks <= 0)
                return;
        }
        m_deltaTime += beatTicks;
    }
}

CBarPosition CBar::offsetPosition(CBarPosition position, qint64 ticks) const
{
    position.ticks += ticks;
    qint64 beatTicks = beatTicksInBar(position.bar);
    if (beatTicks <= 0)
        return position;

    while (position.ticks >= beatTicks)
    {
        position.ticks -= beatTicks;
        if (++position.beat >= beatsInBar(position.bar))
        {
            position.beat = 0;
            position.bar++;
            beatTicks = beatTicksInBar(position.bar);
            if (beatTicks <= 0)
                return position;
        }
    }
    while (position.ticks < 0 && (position.beat > 0 || position.bar > 0))
    {
        if (position.beat > 0)
            position.beat--;
        else
        {
            position.bar--;
            position.beat = beatsInBar(position.bar) - 1;
            beatTicks = beatTicksInBar(position.bar);
            if (beatTicks <= 0)
                return position;
        }
        position.ticks += beatTicks;
    }
    return position;
}

CBarPosition CBar::positionFromBarBeat(int bar, double beat, bool roundUp) const
{
    if (bar < 0)
        bar = 0;
    if (beat < 0.0)
        beat = 0.0;

    const qint64 beatTicks = beatTicksInBar(bar);
    // Beats are typed with two decimals, so an end typed as e.g. 2.33 is meant to
    // include a note at 2 1/3: extend it by the precision of the field.
    if (roundUp)
        beat += END_BEAT_TOLERANCE;
    const int wholeBeats = static_cast<int>(beat);
    // round the fraction down for a start (never after the intended note),
    // up for an end (never before it)
    const double exactTicks = (beat - wholeBeats) * static_cast<double>(beatTicks);
    const auto fractionTicks = static_cast<qint64>(roundUp ? std::ceil(exactTicks) : std::floor(exactTicks));
    // offsetPosition() also moves beats past the end of the bar into the next bars
    return offsetPosition({ bar, 0, 0 }, wholeBeats * beatTicks + fractionTicks);
}

// Advances the time while seeking the start position: to the next beat, or exactly to the
// start position when it is within the current beat.
qint64 CBar::goToBarNumer()
{
    qint64 ticks;
    if (m_barCounter == m_playFrom.bar && m_beatCounter == m_playFrom.beat)
        ticks = m_playFrom.ticks - m_deltaTime;
    else
        ticks = beatTicksInBar(m_barCounter) - m_deltaTime;
    if (ticks <= 0)
        ticks = 1;
    addDeltaTime(ticks);
    return ticks;
}

void CBar::checkGotoBar()
{
    ppDEBUG_BAR(("checkGotoBar currentBarPos %.3f", getCurrentBarPos()));
    if (getCurrentPosition() < m_playFrom)
        m_seekingBarNumber = true;
    else
    {
        if (m_seekingBarNumber == true)
            m_flushTicks = true; // now throw away ticks before we start the music
        m_seekingBarNumber = false;

        // repeat once past the end, so that the notes at the end position are included
        if (m_enableLooping && m_playUpto < getCurrentPosition())
            m_eventBits |= EVENT_BITS_UptoBarReached;
    }
}

void CBar::setPlayFromBar(double bar)
{
    const int wholeBar = static_cast<int>(bar);
    setPlayFrom(positionFromBarBeat(wholeBar, (bar - wholeBar) * beatsInBar(wholeBar)));
}

void CBar::setPlayFrom(const CBarPosition &position)
{
    m_playFrom = position;
    setupEnableFlags();
    checkGotoBar();
}

void CBar::setLoop(bool enabled, const CBarPosition &end)
{
    m_loopEnabled = enabled;
    m_playUpto = end;
    setupEnableFlags();
    checkGotoBar();
}

void CBar::setPlayUptoBar(double endBar)
{
    if (endBar <= 0.0)
    {
        setLoop(false, m_playUpto);
        return;
    }
    const int wholeBar = static_cast<int>(endBar);
    setLoop(true, positionFromBarBeat(wholeBar, (endBar - wholeBar) * beatsInBar(wholeBar)));
}

void CBar::setLoopingBars(double bars)
{
    if (bars <= 0.0)
        setLoop(false, m_playUpto);
    else
        setPlayUptoBar(barPosOfPosition(m_playFrom) + bars);
}
