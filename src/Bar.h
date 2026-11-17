/*********************************************************************************/
/*!
@file           Bar.h

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

#ifndef __BAR_H__
#define __BAR_H__

#include "MidiFile.h"

// The event bits can be ORed together
#define EVENT_BITS_playingStopped          0x0001 // set when we reach the end of piece
#define EVENT_BITS_forceFullRedraw         0x0002 // force the whole screen to be redrawn
#define EVENT_BITS_forceRatingRedraw       0x0004 // force the score to be redrawn
#define EVENT_BITS_newBarNumber            0x0008 // force the bar number to be redrawn
#define EVENT_BITS_UptoBarReached          0x0010 // Used for looping when playing between two bars.
#define EVENT_BITS_loadSong                0x0020 // load song

typedef unsigned long eventBits_t;

// An exact position in the song: the bar, the beat within the bar and the ticks within
// the beat. Bars and beats count from 0 and the ticks are scaled by SPEED_ADJUST_FACTOR,
// like all the time values in CBar. Being whole numbers, positions can be compared exactly.
struct CBarPosition
{
    int bar = 0;
    int beat = 0;
    qint64 ticks = 0;

    bool operator<(const CBarPosition &other) const
    {
        if (bar != other.bar)
            return bar < other.bar;
        if (beat != other.beat)
            return beat < other.beat;
        return ticks < other.ticks;
    }
    bool isSongStart() const { return bar <= 0 && beat <= 0 && ticks <= 0; }
};

// controls the bar numbers
class CBar
{
public:

    CBar() { reset(); }

    // You MUST clear the time sig to 0 first before setting an new start Time Signature
    // at the start of the piece of music
    void setTimeSig(int top, int bottom);

    int getTimeSigTop() {return m_currentTimeSigTop;} // The Numerator
    qint64 getBeatLength() {return m_beatLength;}
    qint64 getBarLength() {return m_barLength;} // in ppqn

    void setPlayFromBar(double bar);
    void setPlayFrom(const CBarPosition &position);
    CBarPosition getPlayFrom() const { return m_playFrom; }
    void reset() {
        setTimeSig( 0 , 0);
        m_playFrom = CBarPosition();
        m_playUpto = CBarPosition();
        m_loopEnabled = false;
        m_seekingBarNumber = false;
        m_flushTicks = false;
        m_eventBits = 0;
        setupEnableFlags();
    }

    // Repeat from the start position up to (and including) the notes at the end position.
    // The end is an exact position of its own: changing the start does not move it.
    void setLoop(bool enabled, const CBarPosition &end);
    bool isLoopEnabled() const { return m_loopEnabled; }
    CBarPosition getPlayUpto() const { return m_playUpto; }
    // true when the loop is enabled and its end is after the start, so it is used
    bool isLoopActive() const { return m_enableLooping; }

    // Old interface, in bars with decimals
    void setPlayUptoBar(double bar);
    double getPlayUptoBar() const { return barPosOfPosition(m_playUpto); }
    void setLoopingBars(double bars);
    double getLoopingBars() const { return m_enableLooping ? getPlayUptoBar() - barPosOfPosition(m_playFrom) : 0.0; }

    void rewind() {
        int top = m_startTimeSigTop;
        int bottom = m_startTimeSigBottom;

        setTimeSig( 0 , 0);
        setTimeSig(top, bottom);
        checkGotoBar();
    }
    void getTimeSig(int *top, int *bottom) {
        *top = m_currentTimeSigTop;
        *bottom = m_currentTimeSigBottom;
    }

    //return true if bar number has changed
    qint64 addDeltaTime(qint64 ticks);

    // Moves the current position by a (possibly negative) number of ticks, without
    // any of the side effects of addDeltaTime(); used to stay in step with the conductor.
    void adjustDeltaTime(qint64 ticks);

    //
    int getBarNumber(){ return m_barCounter;}

    CBarPosition getCurrentPosition() const { return { m_barCounter, m_beatCounter, m_deltaTime }; }

    // Returns the position moved by a (possibly negative) number of ticks,
    // using the current time signature.
    CBarPosition offsetPosition(CBarPosition position, qint64 ticks) const;

    // Converts a bar and a beat (both counting from 0, the beat may have a fraction)
    // to a position, using the current time signature. The fraction is rounded down,
    // so that a start position is never after the note that the user meant; with
    // roundUp it is rounded up, so that an end position is never before it.
    CBarPosition positionFromBarBeat(int bar, double beat, bool roundUp = false) const;
    static constexpr double END_BEAT_TOLERANCE = 0.01; // the precision of the beat fields

    // Returns the beat (counting from 0, with a fraction) of a position.
    double beatOfPosition(const CBarPosition &position) const
    {
        return position.beat + static_cast<double>(position.ticks) /
            static_cast<double>(m_beatLength * SPEED_ADJUST_FACTOR);
    }

    // The position as a bar number with the fraction of the bar as decimals.
    double barPosOfPosition(const CBarPosition &position) const
    {
        return position.bar + beatOfPosition(position) / static_cast<double>(m_currentTimeSigTop);
    }

    double getCurrentBarPos() const { return barPosOfPosition(getCurrentPosition()); }

    bool seekingBarNumber() { return m_seekingBarNumber;}

    // get and reset the current bar event bits
    eventBits_t readEventBits() {
        eventBits_t bits = m_eventBits;
        m_eventBits = 0;
        return bits;
    }

    qint64 goToBarNumer();

private:
    void checkGotoBar();
    void setupEnableFlags()
    {
        m_enableLooping = m_loopEnabled && m_playFrom < m_playUpto;
        m_enablePlayFromBar = (m_enableLooping || !m_playFrom.isSongStart())?true:false;
    }

    // Moves the bar and beat counters so that m_deltaTime is within the current beat.
    void normaliseDeltaTime();

    qint64 m_deltaTime;
    qint64 m_beatLength; //in ppqn ticks
    qint64 m_barLength; // m_beatLength * getTimeSigTop() (also in ppqn ticks)

    int m_startTimeSigTop; // The time Sig at the start of the piece
    int m_startTimeSigBottom;
    int m_currentTimeSigTop; // The current time Sig at the start of the piece
    int m_currentTimeSigBottom;

    int m_barCounter;
    int m_beatCounter;
    CBarPosition m_playFrom;
    CBarPosition m_playUpto;
    bool m_loopEnabled;
    bool m_seekingBarNumber;
    bool m_flushTicks;
    eventBits_t m_eventBits;
    bool m_enableLooping;
    bool m_enablePlayFromBar;

};

#endif  // __BAR_H__

