/*********************************************************************************/
/*!
@file           SongMap.cpp

@brief          The whole of a MIDI file read at once, without playing it.

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

#include "SongMap.h"
#include "MidiByteReader.h"

#include <QFile>
#include <QHash>

#include <algorithm>
#include <cmath>

namespace {

constexpr int DRUM_CHANNEL = 9;

struct TimeSigChange { qint64 tick; int top; int bottom; };
struct TempoChange { qint64 tick; qint64 microsecondsPerQuarter; };

// Notes off first, then the controllers and programmes, then the notes on, so that at the
// same time a repeated note is stopped before it starts again, with the right sound.
int eventOrder(const CSongEvent &event)
{
    const int type = event.status & 0xf0;
    if (type == 0x80 || (type == 0x90 && event.data2 == 0))
        return 0;
    return (type == 0x90) ? 2 : 1;
}

} // namespace

bool CSongMap::load(const QString &fileName)
{
    *this = CSongMap();

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = file.readAll();

    CMidiByteReader header(data, 0, data.size());
    if (header.bytes(4) != "MThd")
        return false;
    const quint32 headerLength = header.word32();
    header.word16(); // format
    const int trackCount = header.word16();
    const int division = header.word16();
    if (!header.ok() || headerLength < 6 || (division & 0x8000) || division == 0)
        return false; // SMPTE time division is not supported
    header.skip(static_cast<qint64>(headerLength) - 6);
    m_ppqn = division;

    std::vector<TimeSigChange> timeSigs;
    std::vector<TempoChange> tempos;

    qsizetype pos = header.pos();
    int track = 0; // counts the MTrk chunks only, like CMidiFile
    for (int chunkIndex = 0; chunkIndex < trackCount && pos + 8 <= data.size(); ++chunkIndex)
    {
        CMidiByteReader chunk(data, pos, data.size());
        const QByteArray chunkId = chunk.bytes(4);
        const qint64 chunkLength = chunk.word32();
        const qsizetype trackStart = chunk.pos();
        const qsizetype trackEnd = std::min<qsizetype>(trackStart + chunkLength, data.size());
        pos = trackEnd;
        if (chunkId != "MTrk")
            continue; // skip unknown chunks

        // the notes that have started and not ended yet, by channel and note
        QHash<int, QVector<int>> sounding;

        CMidiByteReader reader(data, trackStart, trackEnd);
        qint64 tick = 0;
        int runningStatus = 0;
        while (!reader.atEnd() && reader.ok())
        {
            tick += reader.varLength();
            int status = reader.byte();
            if (status < 0x80) // running status: this byte is already data
            {
                if (runningStatus == 0)
                    break;
                status = runningStatus;
                reader.back();
            }

            if (status == 0xff) // meta event
            {
                const int type = reader.byte();
                const qint64 length = reader.varLength();
                const QByteArray meta = reader.bytes(length);
                if (type == 0x2f) // end of track
                    break;
                if (type == 0x51 && meta.size() >= 3)
                    tempos.push_back({ tick, (static_cast<unsigned char>(meta[0]) << 16) |
                                             (static_cast<unsigned char>(meta[1]) << 8) |
                                              static_cast<unsigned char>(meta[2]) });
                else if (type == 0x58 && meta.size() >= 2)
                    timeSigs.push_back({ tick, static_cast<unsigned char>(meta[0]),
                                         1 << std::min<int>(static_cast<unsigned char>(meta[1]), 6) });
                continue;
            }
            if (status == 0xf0 || status == 0xf7) // system exclusive
            {
                reader.skip(reader.varLength());
                continue;
            }

            runningStatus = status;
            const int type = status & 0xf0;
            const int channel = status & 0x0f;
            CSongEvent event;
            event.tick = tick;
            event.status = status;
            event.data1 = reader.byte() & 0x7f;
            if (type != 0xc0 && type != 0xd0)
                event.data2 = reader.byte() & 0x7f;
            if (!reader.ok())
                break;
            m_events.append(event);
            m_lastTick = std::max(m_lastTick, tick);

            const int key = channel * 128 + event.data1;
            if (type == 0x90 && event.data2 > 0)
            {
                CSongNote note;
                note.tick = tick;
                note.note = event.data1;
                note.channel = channel;
                note.track = track;
                note.velocity = event.data2;
                sounding[key].append(m_notes.size());
                m_notes.append(note);
            }
            else if (type == 0x80 || type == 0x90)
            {
                auto found = sounding.find(key);
                if (found != sounding.end() && !found->isEmpty())
                {
                    CSongNote &note = m_notes[found->takeFirst()];
                    note.length = tick - note.tick;
                }
            }
        }
        // notes that never end last up to the end of their track
        for (auto it = sounding.cbegin(); it != sounding.cend(); ++it)
            for (int index : it.value())
                m_notes[index].length = tick - m_notes[index].tick;
        ++track;
    }

    std::stable_sort(m_events.begin(), m_events.end(), [](const CSongEvent &a, const CSongEvent &b) {
        if (a.tick != b.tick)
            return a.tick < b.tick;
        return eventOrder(a) < eventOrder(b);
    });
    std::stable_sort(m_notes.begin(), m_notes.end(), [](const CSongNote &a, const CSongNote &b) { return a.tick < b.tick; });

    bool anyNote = false;
    for (CSongNote &note : m_notes)
    {
        if (note.length <= 0)
            note.length = 1;
        m_maxNoteLength = std::max(m_maxNoteLength, note.length);
        m_lastTick = std::max(m_lastTick, note.tick + note.length);
        if (note.channel == DRUM_CHANNEL)
            continue;
        if (!anyNote)
        {
            m_lowestNote = m_highestNote = note.note;
            anyNote = true;
        }
        m_lowestNote = std::min(m_lowestNote, note.note);
        m_highestNote = std::max(m_highestNote, note.note);
    }

    // The bars, like in CSong::midiFileInfo() and CBar: 4/4 if there is no time signature
    std::stable_sort(timeSigs.begin(), timeSigs.end(), [](const TimeSigChange &a, const TimeSigChange &b) { return a.tick < b.tick; });
    for (const TimeSigChange &change : timeSigs)
        addTimeSigChange(change.tick, change.top, change.bottom);
    if (m_timeSigs.isEmpty())
        addTimeSigChange(0, 4, 4);

    // The tempo changes: 120 BPM until the first one
    std::stable_sort(tempos.begin(), tempos.end(), [](const TempoChange &a, const TempoChange &b) { return a.tick < b.tick; });
    TempoSegment first;
    first.msPerTick = 500.0 / m_ppqn;
    m_tempos.append(first);
    for (const TempoChange &change : tempos)
    {
        if (change.microsecondsPerQuarter <= 0)
            continue;
        TempoSegment segment;
        const TempoSegment &last = m_tempos.last();
        segment.tick = change.tick;
        segment.startMs = last.startMs + static_cast<double>(change.tick - last.tick) * last.msPerTick;
        segment.msPerTick = static_cast<double>(change.microsecondsPerQuarter) / 1000.0 / m_ppqn;
        if (segment.tick == last.tick)
            m_tempos.last() = segment;
        else
            m_tempos.append(segment);
    }

    m_valid = true;
    return true;
}

// The same rules as CBar::addTimeSigChange(), so that the bars are the same as when playing
void CSongMap::addTimeSigChange(qint64 tick, int top, int bottom)
{
    if (top <= 0 || bottom <= 0)
        return;
    TimeSigSegment segment;
    segment.top = top;
    segment.bottom = bottom;
    segment.beatTicks = (static_cast<qint64>(m_ppqn) * 4) / bottom;
    if (segment.beatTicks <= 0)
        return;

    if (!m_timeSigs.isEmpty())
    {
        const TimeSigSegment &last = m_timeSigs.last();
        const qint64 lastBarTicks = last.beatTicks * last.top;
        const qint64 bars = (tick > last.startTick) ? (tick - last.startTick + lastBarTicks - 1) / lastBarTicks : 0;
        segment.startBar = last.startBar + static_cast<int>(bars);
        segment.startTick = last.startTick + bars * lastBarTicks;
        if (segment.startBar == last.startBar)
        {
            m_timeSigs.last() = segment; // replaces a change at the same bar
            return;
        }
        if (last.top == top && last.bottom == bottom)
            return; // nothing changes
    }
    m_timeSigs.append(segment);
}

const CSongMap::TimeSigSegment &CSongMap::segmentOfBar(int bar) const
{
    static const TimeSigSegment defaultSegment = { 0, 0, 4, 4, 96 };
    if (m_timeSigs.isEmpty())
        return defaultSegment;
    int index = 0;
    for (int i = 1; i < m_timeSigs.size() && m_timeSigs[i].startBar <= bar; ++i)
        index = i;
    return m_timeSigs[index];
}

const CSongMap::TimeSigSegment &CSongMap::segmentOfTick(qint64 tick) const
{
    static const TimeSigSegment defaultSegment = { 0, 0, 4, 4, 96 };
    if (m_timeSigs.isEmpty())
        return defaultSegment;
    int index = 0;
    for (int i = 1; i < m_timeSigs.size() && m_timeSigs[i].startTick <= tick; ++i)
        index = i;
    return m_timeSigs[index];
}

int CSongMap::barCount() const
{
    if (m_lastTick <= 0)
        return 1;
    return barOfTick(m_lastTick - 1) + 1;
}

qint64 CSongMap::barStartTick(int bar) const
{
    if (bar <= 0)
        return 0;
    const TimeSigSegment &segment = segmentOfBar(bar);
    return segment.startTick + static_cast<qint64>(bar - segment.startBar) * segment.beatTicks * segment.top;
}

int CSongMap::barOfTick(qint64 tick) const
{
    if (tick <= 0)
        return 0;
    const TimeSigSegment &segment = segmentOfTick(tick);
    return segment.startBar + static_cast<int>((tick - segment.startTick) / (segment.beatTicks * segment.top));
}

int CSongMap::beatsInBar(int bar) const
{
    return segmentOfBar(bar).top;
}

int CSongMap::timeSigBottom(int bar) const
{
    return segmentOfBar(bar).bottom;
}

qint64 CSongMap::beatTicks(int bar) const
{
    return segmentOfBar(bar).beatTicks;
}

CBarPosition CSongMap::positionOfTick(qint64 tick) const
{
    if (tick < 0)
        tick = 0;
    CBarPosition position;
    position.bar = barOfTick(tick);
    const qint64 beat = beatTicks(position.bar);
    const qint64 inBar = tick - barStartTick(position.bar);
    position.beat = static_cast<int>(inBar / beat);
    position.ticks = (inBar % beat) * SPEED_ADJUST_FACTOR;
    return position;
}

qint64 CSongMap::tickOfPosition(const CBarPosition &position) const
{
    const int bar = std::max(position.bar, 0);
    qint64 tick = barStartTick(bar) + static_cast<qint64>(std::max(position.beat, 0)) * beatTicks(bar);
    if (position.ticks > 0)
        tick += position.ticks / SPEED_ADJUST_FACTOR;
    return tick;
}

qint64 CSongMap::tickOfBarBeat(int bar, double beat) const
{
    bar = std::max(bar, 0);
    beat = std::max(beat, 0.0);
    // the beats past the end of the bar go on into the next bars, with their own time signature
    while (beat >= beatsInBar(bar) && bar < barCount() + 1000)
    {
        beat -= beatsInBar(bar);
        ++bar;
    }
    return barStartTick(bar) + static_cast<qint64>(std::llround(beat * static_cast<double>(beatTicks(bar))));
}

double CSongMap::beatOfTick(qint64 tick) const
{
    const CBarPosition position = positionOfTick(tick);
    return position.beat + static_cast<double>(position.ticks) / static_cast<double>(beatTicks(position.bar) * SPEED_ADJUST_FACTOR);
}

double CSongMap::msOfTick(qint64 tick) const
{
    if (m_tempos.isEmpty())
        return 0.0;
    int index = 0;
    for (int i = 1; i < m_tempos.size() && m_tempos[i].tick <= tick; ++i)
        index = i;
    const TempoSegment &segment = m_tempos[index];
    return segment.startMs + static_cast<double>(tick - segment.tick) * segment.msPerTick;
}

qint64 CSongMap::tickOfMs(double ms) const
{
    if (m_tempos.isEmpty())
        return 0;
    int index = 0;
    for (int i = 1; i < m_tempos.size() && m_tempos[i].startMs <= ms; ++i)
        index = i;
    const TempoSegment &segment = m_tempos[index];
    if (segment.msPerTick <= 0.0)
        return segment.tick;
    return segment.tick + static_cast<qint64>(std::floor((ms - segment.startMs) / segment.msPerTick));
}
