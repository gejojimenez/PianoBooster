/*********************************************************************************/
/*!
@file           SongMap.h

@brief          The whole of a MIDI file read at once, without playing it: its notes,
                its bars (with the time signature changes) and its tempo changes.
                Used by the section editor to show any part of the song at any time.

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

#ifndef __SONG_MAP_H__
#define __SONG_MAP_H__

#include <QString>
#include <QVector>

#include "Bar.h"

// A note of the song, with its times in ticks of the file (not scaled)
struct CSongNote
{
    qint64 tick = 0;
    qint64 length = 0;
    int note = 0;
    int channel = 0;
    int track = 0;
    int velocity = 0;
};

// A channel message of the song (note, controller, programme, pitch bend ...), to play it
struct CSongEvent
{
    qint64 tick = 0;
    int status = 0;  // the type ORed with the channel, e.g. 0x93
    int data1 = 0;
    int data2 = 0;
};

class CSongMap
{
public:
    // Reads a MIDI file; returns false if it can't be read.
    bool load(const QString &fileName);
    bool isValid() const { return m_valid; }

    int ppqn() const { return m_ppqn; }
    const QVector<CSongNote> &notes() const { return m_notes; }   // in order of time
    const QVector<CSongEvent> &events() const { return m_events; } // in order of time
    qint64 maxNoteLength() const { return m_maxNoteLength; }
    qint64 lastTick() const { return m_lastTick; }
    int lowestNote() const { return m_lowestNote; }   // drums are not counted
    int highestNote() const { return m_highestNote; }

    // Bars count from 0, like in CBar. A time signature change within a bar starts
    // at the next bar, like in CBar::addTimeSigChange().
    int barCount() const;                  // the bars up to the end of the last note
    qint64 endTick() const { return barStartTick(barCount()); } // the end of the last bar
    qint64 barStartTick(int bar) const;
    int barOfTick(qint64 tick) const;
    int beatsInBar(int bar) const;
    int timeSigBottom(int bar) const;
    qint64 beatTicks(int bar) const;       // the length of a beat of that bar
    qint64 barTicks(int bar) const { return beatTicks(bar) * beatsInBar(bar); }

    // Conversions between ticks of the file and positions (whose ticks are scaled by
    // SPEED_ADJUST_FACTOR). tickOfPosition() rounds down.
    CBarPosition positionOfTick(qint64 tick) const;
    qint64 tickOfPosition(const CBarPosition &position) const;
    // A bar and a beat (both counting from 0, the beat may have a fraction), rounded to
    // the nearest tick. Beats past the end of the bar go on into the next bars.
    qint64 tickOfBarBeat(int bar, double beat) const;
    double beatOfTick(qint64 tick) const;  // the beat within its bar, counting from 0

    // The end of a section (CSongPart, CBar::setLoop()) is the position of its last notes,
    // which are included. Here a section is a stretch that ends just before a tick, so
    // its end position is one tick before that, and the notes at that tick are not included.
    CBarPosition sectionEndPosition(qint64 endTick) const { return positionOfTick(endTick - 1); }
    qint64 sectionEndTick(const CBarPosition &end) const { return tickOfPosition(end) + 1; }

    // Time, at normal speed, using the tempo changes
    double msOfTick(qint64 tick) const;
    qint64 tickOfMs(double ms) const;

private:
    struct TimeSigSegment
    {
        int startBar = 0;
        qint64 startTick = 0;
        int top = 4;
        int bottom = 4;
        qint64 beatTicks = 0;
    };
    struct TempoSegment
    {
        qint64 tick = 0;
        double startMs = 0.0;
        double msPerTick = 0.0;
    };

    void addTimeSigChange(qint64 tick, int top, int bottom);
    const TimeSigSegment &segmentOfBar(int bar) const;
    const TimeSigSegment &segmentOfTick(qint64 tick) const;

    bool m_valid = false;
    int m_ppqn = 96;
    QVector<CSongNote> m_notes;
    QVector<CSongEvent> m_events;
    QVector<TimeSigSegment> m_timeSigs;  // never empty once loaded
    QVector<TempoSegment> m_tempos;      // never empty once loaded
    qint64 m_maxNoteLength = 0;
    qint64 m_lastTick = 0;
    int m_lowestNote = 60;
    int m_highestNote = 72;
};

#endif // __SONG_MAP_H__
