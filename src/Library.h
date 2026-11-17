/*********************************************************************************/
/*!
@file           Library.h

@brief          The song library: the locations (folders) with MIDI files and what
                is known about each song (favourite, when it was last opened, ...),
                kept in an SQLite database next to the settings.

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

#ifndef __LIBRARY_H__
#define __LIBRARY_H__

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

#include "MidiInfo.h"

// What the library knows about a song
struct CLibrarySong
{
    QString path;           // absolute path, with '/' separators
    QString hash;           // SHA-1 of the file contents, identifies the song if it is moved
    bool favourite = false;
    QDateTime lastOpened;   // invalid if never opened
    int timesOpened = 0;
    CMidiInfo info;         // info.valid is false if not known yet
};

class CLibrary
{
public:
    // Opens (creating it if needed) the database in the given folder.
    explicit CLibrary(const QString &folder);
    ~CLibrary();
    bool isOpen() const { return m_open; }

    // Locations: folders where the MIDI files are
    QStringList locations() const;
    void addLocation(const QString &folder);
    void removeLocation(const QString &folder);

    // Records that a song has been opened, with its MIDI information.
    // A song moved or renamed since it was last seen keeps its data.
    void songOpened(const QString &path, const CMidiInfo &info);

    void setFavourite(const QString &path, bool favourite);

    // Stores the MIDI information of a song, without counting it as opened.
    void storeInfo(const QString &path, const CMidiInfo &info);

    // Groups many changes in one transaction, which is much faster.
    void beginBatch();
    void endBatch();

    // The songs the library knows, by path (only those found in the database).
    CLibrarySong song(const QString &path) const;
    QVector<CLibrarySong> songs(const QStringList &paths) const;
    QVector<CLibrarySong> favourites() const;
    QVector<CLibrarySong> recent(int maximum) const;

    static QStringList midiFileFilters() { return { "*.mid", "*.midi", "*.kar" }; }
    static QString normalisePath(const QString &path);
    static QString fileHash(const QString &path);

private:
    bool exec(const QString &sql);
    QVector<CLibrarySong> query(const QString &where, const QVariantList &values = {}, const QString &tail = QString()) const;

    QString m_connection;
    bool m_open = false;
};

#endif // __LIBRARY_H__
