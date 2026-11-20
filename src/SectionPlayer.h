/*********************************************************************************/
/*!
@file           SectionPlayer.h

@brief          Plays a stretch of a song (e.g. a section being edited) through the
                MIDI output, to hear where it starts and ends. It plays all the
                channels, as in "Listen", and does not use the conductor, which
                must be stopped while it plays.

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

#ifndef __SECTION_PLAYER_H__
#define __SECTION_PLAYER_H__

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include "SongMap.h"

class CMidiDevice;

class CSectionPlayer : public QObject
{
    Q_OBJECT

public:
    CSectionPlayer(const CSongMap *map, CMidiDevice *device, QObject *parent = nullptr);
    ~CSectionPlayer() override;

    // Plays from the start tick up to (not including) the end tick, at a speed
    // (1.0 is the normal speed), starting again at the end if repeat is true.
    void play(qint64 startTick, qint64 endTick, bool repeat, double speed);
    void stop();
    bool isPlaying() const { return m_timer.isActive(); }
    void setRepeat(bool repeat) { m_repeat = repeat; }
    void setSpeed(double speed);

signals:
    void positionChanged(qint64 tick); // a few times a second while playing
    void stopped();

private slots:
    void timerTick();

private:
    void restart();
    void chaseControllers(qint64 tick);
    void silence();
    void send(const CSongEvent &event);

    const CSongMap *m_map;
    CMidiDevice *m_device;
    QTimer m_timer;
    QElapsedTimer m_clock;
    qint64 m_startTick = 0;
    qint64 m_endTick = 0;
    bool m_repeat = false;
    double m_speed = 1.0;
    double m_clockStartMs = 0.0; // the song time when the clock was started
    int m_nextEvent = 0;
    qint64 m_lastPositionMs = 0;
    bool m_sounding[16][128];
};

#endif // __SECTION_PLAYER_H__
