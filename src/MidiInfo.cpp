/*********************************************************************************/
/*!
@file           MidiInfo.cpp

@brief          Reads general information from a MIDI file (time signature, key,
                tempo, duration and number of bars) without playing it.

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

#include "MidiInfo.h"
#include "MidiByteReader.h"

#include <QCoreApplication>
#include <QFile>
#include <QLocale>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

struct TempoChange { qint64 tick; qint64 microsecondsPerQuarter; };
struct TimeSigChange { qint64 tick; int top; int bottom; };

using ByteReader = CMidiByteReader;

QString tr(const char *text)
{
    return QCoreApplication::translate("CMidiInfo", text);
}

} // namespace

CMidiInfo CMidiInfo::read(const QString &fileName)
{
    CMidiInfo info;

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly))
        return info;
    const QByteArray data = file.readAll();

    ByteReader header(data, 0, data.size());
    if (header.bytes(4) != "MThd")
        return info;
    const quint32 headerLength = header.word32();
    header.word16(); // format
    const int trackCount = header.word16();
    const int division = header.word16();
    if (!header.ok() || headerLength < 6 || (division & 0x8000) || division == 0)
        return info; // SMPTE time division is not supported
    header.skip(static_cast<qint64>(headerLength) - 6);

    std::vector<TempoChange> tempos;
    std::vector<TimeSigChange> timeSigs;
    qint64 keyTick = -1;
    qint64 lastTick = 0;

    qsizetype pos = header.pos();
    for (int track = 0; track < trackCount && pos + 8 <= data.size(); ++track)
    {
        ByteReader chunk(data, pos, data.size());
        const QByteArray chunkId = chunk.bytes(4);
        const qint64 chunkLength = chunk.word32();
        const qsizetype trackStart = chunk.pos();
        const qsizetype trackEnd = std::min<qsizetype>(trackStart + chunkLength, data.size());
        pos = trackEnd;
        if (chunkId != "MTrk")
            continue; // skip unknown chunks

        ByteReader reader(data, trackStart, trackEnd);
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
                else if (type == 0x59 && meta.size() >= 2 && (keyTick < 0 || tick < keyTick))
                {
                    keyTick = tick;
                    info.keySharps = static_cast<signed char>(meta[0]);
                    info.keyMinor = meta[1] != 0;
                }
                else if (type == 0x03 && track == 0 && info.title.isEmpty())
                    info.title = QString::fromLatin1(meta).trimmed();
            }
            else if (status == 0xf0 || status == 0xf7) // system exclusive
            {
                reader.skip(reader.varLength());
            }
            else
            {
                runningStatus = status;
                const int type = status & 0xf0;
                reader.skip((type == 0xc0 || type == 0xd0) ? 1 : 2);
                lastTick = std::max(lastTick, tick);
            }
        }
    }

    if (info.keySharps < -7 || info.keySharps > 7)
        info.keySharps = NO_KEY;

    // Duration, using the tempo changes
    std::stable_sort(tempos.begin(), tempos.end(), [](const TempoChange &a, const TempoChange &b) { return a.tick < b.tick; });
    double microseconds = 0.0;
    qint64 segmentTick = 0;
    qint64 microsecondsPerQuarter = 500000; // 120 BPM until the first tempo event
    for (const TempoChange &change : tempos)
    {
        if (change.tick >= lastTick)
            break;
        microseconds += static_cast<double>(change.tick - segmentTick) * static_cast<double>(microsecondsPerQuarter) / division;
        segmentTick = change.tick;
        if (change.microsecondsPerQuarter > 0)
            microsecondsPerQuarter = change.microsecondsPerQuarter;
    }
    microseconds += static_cast<double>(lastTick - segmentTick) * static_cast<double>(microsecondsPerQuarter) / division;
    info.durationMs = static_cast<qint64>(microseconds / 1000.0);

    if (!tempos.empty() && tempos.front().microsecondsPerQuarter > 0)
        info.tempoBpm = 60000000.0 / static_cast<double>(tempos.front().microsecondsPerQuarter);
    info.tempoChanges = std::count_if(tempos.begin(), tempos.end(), [&](const TempoChange &t) {
        return t.microsecondsPerQuarter != tempos.front().microsecondsPerQuarter && t.tick < lastTick; }) > 0;

    // Number of bars, using the time signature changes (each one starts a new bar)
    std::stable_sort(timeSigs.begin(), timeSigs.end(), [](const TimeSigChange &a, const TimeSigChange &b) { return a.tick < b.tick; });
    if (!timeSigs.empty() && timeSigs.front().top > 0)
    {
        info.timeSigTop = timeSigs.front().top;
        info.timeSigBottom = timeSigs.front().bottom;
    }
    int bars = 0;
    qint64 sigTick = 0;
    int top = 4, bottom = 4;
    auto barsUntil = [&](qint64 endTick, bool roundUp) {
        const qint64 barTicks = static_cast<qint64>(division) * 4 * top / bottom;
        if (barTicks <= 0 || endTick <= sigTick)
            return 0;
        const qint64 ticks = endTick - sigTick;
        return static_cast<int>(roundUp ? (ticks + barTicks - 1) / barTicks : (ticks + barTicks / 2) / barTicks);
    };
    for (const TimeSigChange &change : timeSigs)
    {
        if (change.top <= 0)
            continue;
        if (change.tick >= lastTick)
            break;
        if (change.tick > sigTick)
        {
            bars += barsUntil(change.tick, false);
            if (change.top != top || change.bottom != bottom)
                info.timeSigChanges = true;
        }
        sigTick = change.tick;
        top = change.top;
        bottom = change.bottom;
    }
    // the last note may end in the middle of a bar, which still counts as a bar
    bars += barsUntil(std::max(lastTick, sigTick + 1), true);
    info.bars = bars;

    info.valid = true;
    return info;
}

QString CMidiInfo::timeSigText() const
{
    QString text = QString("%1/%2").arg(timeSigTop).arg(timeSigBottom);
    if (timeSigChanges)
        text += " " + tr("(changes)");
    return text;
}

QString CMidiInfo::keyText() const
{
    if (keySharps == NO_KEY)
        return QString();
    // index 0 is 7 flats, 14 is 7 sharps
    static const char *majorKeys[] = { "Cb", "Gb", "Db", "Ab", "Eb", "Bb", "F", "C", "G", "D", "A", "E", "B", "F#", "C#" };
    static const char *minorKeys[] = { "Ab", "Eb", "Bb", "F", "C", "G", "D", "A", "E", "B", "F#", "C#", "G#", "D#", "A#" };
    const QString key = keyMinor ? minorKeys[keySharps + 7] : majorKeys[keySharps + 7];

    // translate the note letter and show the accidental as a symbol
    static const char *letters[] = {
        QT_TRANSLATE_NOOP("CMidiInfo", "C"), QT_TRANSLATE_NOOP("CMidiInfo", "D"), QT_TRANSLATE_NOOP("CMidiInfo", "E"),
        QT_TRANSLATE_NOOP("CMidiInfo", "F"), QT_TRANSLATE_NOOP("CMidiInfo", "G"), QT_TRANSLATE_NOOP("CMidiInfo", "A"),
        QT_TRANSLATE_NOOP("CMidiInfo", "B") };
    const int letterIndex = QString("CDEFGAB").indexOf(key[0]);
    QString name = tr(letters[letterIndex]);
    if (key.size() > 1)
        name += (key[1] == '#') ? QString::fromUtf8("♯") : QString::fromUtf8("♭");

    return keyMinor ? tr("%1 minor").arg(name) : tr("%1 major").arg(name);
}

QString CMidiInfo::tempoText() const
{
    QString text = tr("%1 BPM").arg(qRound(tempoBpm));
    if (tempoChanges)
        text += " " + tr("(changes)");
    return text;
}

QString CMidiInfo::durationText() const
{
    const qint64 seconds = (durationMs + 500) / 1000;
    return QString("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
}
