/*********************************************************************************/
/*!
@file           MidiInfo.h

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

#ifndef __MIDI_INFO_H__
#define __MIDI_INFO_H__

#include <QString>

class CMidiInfo
{
public:
    static constexpr int NO_KEY = 99;

    bool valid = false;
    QString title;              // from the first track name, may be empty
    int timeSigTop = 4;         // the first time signature
    int timeSigBottom = 4;
    bool timeSigChanges = false;
    int keySharps = NO_KEY;     // the first key signature: -7 (7 flats) .. 7 (7 sharps)
    bool keyMinor = false;
    double tempoBpm = 120.0;    // the first tempo, in quarter notes per minute
    bool tempoChanges = false;
    qint64 durationMs = 0;
    int bars = 0;

    // Reads the information from a MIDI file; valid is false if it can't be read.
    static CMidiInfo read(const QString &fileName);

    // Texts to show to the user (translated)
    QString timeSigText() const;
    QString keyText() const;
    QString tempoText() const;
    QString durationText() const;
};

#endif // __MIDI_INFO_H__
