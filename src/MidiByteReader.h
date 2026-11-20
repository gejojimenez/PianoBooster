/*********************************************************************************/
/*!
@file           MidiByteReader.h

@brief          A simple reader of the bytes of a MIDI file that stops safely at the
                end of the data. Used to read MIDI files without playing them.

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

#ifndef __MIDI_BYTE_READER_H__
#define __MIDI_BYTE_READER_H__

#include <QByteArray>

class CMidiByteReader
{
public:
    CMidiByteReader(const QByteArray &data, qsizetype start, qsizetype end) : m_data(data), m_pos(start), m_end(end) {}
    bool atEnd() const { return m_pos >= m_end; }
    bool ok() const { return m_ok; }
    qsizetype pos() const { return m_pos; }
    void skip(qint64 count)
    {
        if (count < 0 || count > m_end - m_pos) { m_ok = false; m_pos = m_end; return; }
        m_pos += count;
    }
    void back() { if (m_pos > 0) --m_pos; }
    int byte()
    {
        if (m_pos >= m_end) { m_ok = false; return 0; }
        return static_cast<unsigned char>(m_data[m_pos++]);
    }
    quint32 word32() { quint32 v = 0; for (int i = 0; i < 4; ++i) v = (v << 8) | static_cast<quint32>(byte()); return v; }
    quint16 word16() { const auto hi = static_cast<quint16>(byte()); return static_cast<quint16>((hi << 8) | static_cast<quint16>(byte())); }
    qint64 varLength()
    {
        qint64 value = 0;
        for (int i = 0; i < 4; ++i)
        {
            const int b = byte();
            value = (value << 7) | (b & 0x7f);
            if ((b & 0x80) == 0)
                return value;
        }
        m_ok = false;
        return 0;
    }
    QByteArray bytes(qint64 count)
    {
        if (count < 0 || count > m_end - m_pos) { m_ok = false; m_pos = m_end; return QByteArray(); }
        QByteArray result = m_data.mid(m_pos, count);
        m_pos += count;
        return result;
    }

private:
    const QByteArray &m_data;
    qsizetype m_pos;
    qsizetype m_end;
    bool m_ok = true;
};

#endif // __MIDI_BYTE_READER_H__
