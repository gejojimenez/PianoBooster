/*********************************************************************************/
/*!
@file           SectionPlayer.cpp

@brief          Plays a stretch of a song through the MIDI output.

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

#include "SectionPlayer.h"
#include "MidiDevice.h"

#include <algorithm>
#include <cstring>

namespace {
constexpr int TIMER_INTERVAL_MS = 2;
constexpr int POSITION_INTERVAL_MS = 30;
}

CSectionPlayer::CSectionPlayer(const CSongMap *map, CMidiDevice *device, QObject *parent)
    : QObject(parent), m_map(map), m_device(device)
{
    std::memset(m_sounding, 0, sizeof(m_sounding));
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(TIMER_INTERVAL_MS);
    connect(&m_timer, &QTimer::timeout, this, &CSectionPlayer::timerTick);
}

CSectionPlayer::~CSectionPlayer()
{
    if (isPlaying())
    {
        m_timer.stop();
        silence();
    }
}

void CSectionPlayer::play(qint64 startTick, qint64 endTick, bool repeat, double speed)
{
    if (isPlaying())
    {
        m_timer.stop();
        silence();
    }
    if (!m_map || !m_device || endTick <= startTick)
        return;
    m_startTick = startTick;
    m_endTick = endTick;
    m_repeat = repeat;
    m_speed = std::max(speed, 0.1);
    // the sounds, volumes and pedals that the song has set up by the start
    chaseControllers(startTick);
    restart();
    m_timer.start();
}

void CSectionPlayer::stop()
{
    if (!isPlaying())
        return;
    m_timer.stop();
    silence();
    emit stopped();
}

void CSectionPlayer::setSpeed(double speed)
{
    speed = std::max(speed, 0.1);
    if (isPlaying())
    {
        // carry on from the same place in the song
        const double songMs = m_clockStartMs + static_cast<double>(m_clock.elapsed()) * m_speed;
        m_clockStartMs = songMs;
        m_clock.restart();
    }
    m_speed = speed;
}

void CSectionPlayer::restart()
{
    const QVector<CSongEvent> &events = m_map->events();
    m_nextEvent = static_cast<int>(std::lower_bound(events.begin(), events.end(), m_startTick,
        [](const CSongEvent &event, qint64 tick) { return event.tick < tick; }) - events.begin());
    m_clockStartMs = m_map->msOfTick(m_startTick);
    m_clock.start();
    m_lastPositionMs = -POSITION_INTERVAL_MS;
}

void CSectionPlayer::timerTick()
{
    const QVector<CSongEvent> &events = m_map->events();
    const qint64 elapsed = m_clock.elapsed();
    const qint64 now = m_map->tickOfMs(m_clockStartMs + static_cast<double>(elapsed) * m_speed);

    while (m_nextEvent < events.size() && events[m_nextEvent].tick <= now && events[m_nextEvent].tick < m_endTick)
        send(events[m_nextEvent++]);

    if (now >= m_endTick)
    {
        silence();
        if (m_repeat)
        {
            chaseControllers(m_startTick); // e.g. a pedal pressed before the start
            restart();
            emit positionChanged(m_startTick);
            return;
        }
        m_timer.stop();
        emit positionChanged(m_endTick);
        emit stopped();
        return;
    }
    if (elapsed - m_lastPositionMs >= POSITION_INTERVAL_MS)
    {
        m_lastPositionMs = elapsed;
        emit positionChanged(now);
    }
}

// Sends the last programme, controllers and pitch bend of each channel before a time,
// without the notes, so that the section sounds as it does when the song is played.
void CSectionPlayer::chaseControllers(qint64 tick)
{
    int programme[16];
    int controller[16][128];
    int bendLsb[16];
    int bendMsb[16];
    std::fill(std::begin(programme), std::end(programme), -1);
    std::fill(std::begin(bendLsb), std::end(bendLsb), -1);
    std::fill(std::begin(bendMsb), std::end(bendMsb), -1);
    for (auto &values : controller)
        std::fill(std::begin(values), std::end(values), -1);

    for (const CSongEvent &event : m_map->events())
    {
        if (event.tick >= tick)
            break;
        const int channel = event.status & 0x0f;
        switch (event.status & 0xf0)
        {
        case MIDI_PROGRAM_CHANGE:
            programme[channel] = event.data1;
            break;
        case MIDI_CONTROL_CHANGE:
            if (event.data1 < MIDI_ALL_SOUND_OFF) // not the channel mode messages
                controller[channel][event.data1] = event.data2;
            break;
        case MIDI_PITCH_BEND:
            bendLsb[channel] = event.data1;
            bendMsb[channel] = event.data2;
            break;
        default:
            break;
        }
    }

    for (int channel = 0; channel < 16; ++channel)
    {
        CMidiEvent midi;
        if (programme[channel] >= 0)
        {
            midi.programChangeEvent(0, channel, programme[channel]);
            m_device->playMidiEvent(midi);
        }
        for (int number = 0; number < 128; ++number)
        {
            if (controller[channel][number] < 0)
                continue;
            midi.controlChangeEvent(0, channel, number, controller[channel][number]);
            m_device->playMidiEvent(midi);
        }
        if (bendMsb[channel] >= 0)
        {
            midi.pitchBendEvent(0, channel, bendLsb[channel], bendMsb[channel]);
            m_device->playMidiEvent(midi);
        }
    }
}

void CSectionPlayer::send(const CSongEvent &event)
{
    const int channel = event.status & 0x0f;
    CMidiEvent midi;
    switch (event.status & 0xf0)
    {
    case MIDI_NOTE_OFF:
        midi.noteOffEvent(0, channel, event.data1, event.data2);
        m_sounding[channel][event.data1] = false;
        break;
    case MIDI_NOTE_ON:
        if (event.data2 == 0)
        {
            midi.noteOffEvent(0, channel, event.data1, 0);
            m_sounding[channel][event.data1] = false;
        }
        else
        {
            midi.noteOnEvent(0, channel, event.data1, event.data2);
            m_sounding[channel][event.data1] = true;
        }
        break;
    case MIDI_NOTE_PRESSURE:
        midi.notePressure(0, channel, event.data1, event.data2);
        break;
    case MIDI_CONTROL_CHANGE:
        midi.controlChangeEvent(0, channel, event.data1, event.data2);
        break;
    case MIDI_PROGRAM_CHANGE:
        midi.programChangeEvent(0, channel, event.data1);
        break;
    case MIDI_CHANNEL_PRESSURE:
        midi.channelPressure(0, channel, event.data1);
        break;
    case MIDI_PITCH_BEND:
        midi.pitchBendEvent(0, channel, event.data1, event.data2);
        break;
    default:
        return;
    }
    m_device->playMidiEvent(midi);
}

// Stops every note that is sounding, and lifts the pedal
void CSectionPlayer::silence()
{
    CMidiEvent midi;
    for (int channel = 0; channel < 16; ++channel)
    {
        for (int note = 0; note < 128; ++note)
        {
            if (!m_sounding[channel][note])
                continue;
            midi.noteOffEvent(0, channel, note, 0);
            m_device->playMidiEvent(midi);
            m_sounding[channel][note] = false;
        }
        midi.controlChangeEvent(0, channel, MIDI_SUSTAIN, 0);
        m_device->playMidiEvent(midi);
        midi.controlChangeEvent(0, channel, MIDI_ALL_NOTES_OFF, 0);
        m_device->playMidiEvent(midi);
    }
}
