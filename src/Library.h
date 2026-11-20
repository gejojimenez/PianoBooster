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
#include "Bar.h"

// What the library knows about a song
struct CLibrarySong
{
    QString path;           // absolute path, with '/' separators
    QString hash;           // SHA-1 of the file contents, identifies the song if it is moved
    bool favourite = false;
    QDateTime lastOpened;   // invalid if never opened
    int timesOpened = 0;
    CMidiInfo info;         // info.valid is false if not known yet
    int partCount = 0;      // number of named parts
};

// How a song is practised. The settings belong to the contents of the song: copies of
// the same song in different folders share them.
struct CSongSettings
{
    bool valid = false;     // false if the song has no settings yet: these are the defaults
    QString hand = "both";  // "left", "right" or "both"
    int speed = 100;        // percentage
    int leftChannel = -2;   // MIDI channel of each hand: -1 for none, -2 for not set
    int rightChannel = -2;
};

// A named part of a song, to practise it on its own: played from start to end (included)
struct CSongPart
{
    qint64 id = -1;
    QString name;
    CBarPosition start;
    CBarPosition end;
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

    // The named parts of a song, in the order they were created. They belong to the song,
    // not to its path, so they are kept if the song is moved or renamed.
    QVector<CSongPart> parts(const QString &songPath) const;
    qint64 addPart(const QString &songPath, const QString &name, const CBarPosition &start, const CBarPosition &end);
    void updatePart(const CSongPart &part);
    void deletePart(qint64 id);

    // The settings of a song, or the defaults if it has none
    CSongSettings songSettings(const QString &songPath) const;
    // Saves the settings of a song, and of all its copies
    void setSongSettings(const QString &songPath, const CSongSettings &settings);

    // The file name of the last song opened in a folder, empty if none
    QString lastSong(const QString &folder) const;
    void setLastSong(const QString &folder, const QString &songName);

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
    bool addColumn(const QString &table, const QString &column, const QString &type);
    qint64 findOrCreateSong(const QString &path, const QString &hash);
    qint64 songId(const QString &path, bool create) const;
    QVector<CLibrarySong> query(const QString &where, const QVariantList &values = {}, const QString &tail = QString()) const;

    QString m_connection;
    bool m_open = false;
};

#endif // __LIBRARY_H__
