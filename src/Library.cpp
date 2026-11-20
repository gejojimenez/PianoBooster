/*********************************************************************************/
/*!
@file           Library.cpp

@brief          The song library, kept in an SQLite database next to the settings.

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

#include "Library.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "Util.h"

namespace {
    const char *SONG_COLUMNS =
        "path, hash, favourite, last_opened, times_opened, title, time_sig_top, time_sig_bottom, "
        "time_sig_changes, key_sharps, key_minor, tempo_bpm, tempo_changes, duration_ms, bars, "
        "(SELECT COUNT(*) FROM parts WHERE parts.song_id = songs.id)";
}

CLibrary::CLibrary(const QString &folder)
{
    static int connectionCount = 0;
    m_connection = QString("pianobooster_library_%1").arg(++connectionCount);

    QDir().mkpath(folder);
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", m_connection);
    db.setDatabaseName(QDir(folder).filePath("library.db"));
    if (!db.open())
    {
        ppLogError("Cannot open the library database: %s", qPrintable(db.lastError().text()));
        return;
    }

    m_open = exec("CREATE TABLE IF NOT EXISTS locations (path TEXT PRIMARY KEY)") &&
             exec("CREATE TABLE IF NOT EXISTS songs ("
                  "id INTEGER PRIMARY KEY, path TEXT UNIQUE NOT NULL, hash TEXT, "
                  "favourite INTEGER NOT NULL DEFAULT 0, last_opened TEXT, times_opened INTEGER NOT NULL DEFAULT 0, "
                  "title TEXT, time_sig_top INTEGER, time_sig_bottom INTEGER, time_sig_changes INTEGER, "
                  "key_sharps INTEGER, key_minor INTEGER, tempo_bpm REAL, tempo_changes INTEGER, "
                  "duration_ms INTEGER, bars INTEGER, info_valid INTEGER NOT NULL DEFAULT 0)") &&
             exec("CREATE INDEX IF NOT EXISTS songs_hash ON songs(hash)") &&
             exec("CREATE TABLE IF NOT EXISTS parts ("
                  "id INTEGER PRIMARY KEY, song_id INTEGER NOT NULL REFERENCES songs(id) ON DELETE CASCADE, "
                  "name TEXT NOT NULL, start_bar INTEGER, start_beat INTEGER, start_ticks INTEGER, "
                  "end_bar INTEGER, end_beat INTEGER, end_ticks INTEGER)") &&
             exec("CREATE INDEX IF NOT EXISTS parts_song ON parts(song_id)") &&
             // version 3: the settings to practise each song, and the last song of each folder
             addColumn("songs", "hand", "TEXT") &&
             addColumn("songs", "speed", "INTEGER") &&
             addColumn("songs", "left_channel", "INTEGER") &&
             addColumn("songs", "right_channel", "INTEGER") &&
             exec("CREATE TABLE IF NOT EXISTS folders (path TEXT PRIMARY KEY, last_song TEXT)") &&
             exec("PRAGMA foreign_keys = ON") &&
             exec("PRAGMA user_version = 3");
}

// Adds a column to a table unless it already has it (made by an older version)
bool CLibrary::addColumn(const QString &table, const QString &column, const QString &type)
{
    QSqlQuery q(QSqlDatabase::database(m_connection));
    if (!q.exec(QString("PRAGMA table_info(%1)").arg(table)))
        return false;
    while (q.next())
        if (q.value(1).toString() == column)
            return true;
    return exec(QString("ALTER TABLE %1 ADD COLUMN %2 %3").arg(table, column, type));
}

CLibrary::~CLibrary()
{
    {
        QSqlDatabase db = QSqlDatabase::database(m_connection, false);
        if (db.isOpen())
            db.close();
    }
    QSqlDatabase::removeDatabase(m_connection);
}

bool CLibrary::exec(const QString &sql)
{
    QSqlQuery q(QSqlDatabase::database(m_connection));
    if (!q.exec(sql))
    {
        ppLogError("Library database error: %s", qPrintable(q.lastError().text()));
        return false;
    }
    return true;
}

QString CLibrary::normalisePath(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString CLibrary::fileHash(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(&file);
    return QString::fromLatin1(hash.result().toHex());
}

QStringList CLibrary::locations() const
{
    QStringList result;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    if (q.exec("SELECT path FROM locations ORDER BY path COLLATE NOCASE"))
        while (q.next())
            result << q.value(0).toString();
    return result;
}

void CLibrary::addLocation(const QString &folder)
{
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare("INSERT OR IGNORE INTO locations (path) VALUES (?)");
    q.addBindValue(normalisePath(folder));
    q.exec();
}

void CLibrary::removeLocation(const QString &folder)
{
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare("DELETE FROM locations WHERE path = ?");
    q.addBindValue(normalisePath(folder));
    q.exec();
}

// Finds the song by its path, or else by its contents if it has been moved or renamed,
// or else adds it. Returns its id, or -1 on error.
qint64 CLibrary::findOrCreateSong(const QString &path, const QString &hash)
{
    QSqlDatabase db = QSqlDatabase::database(m_connection);
    QSqlQuery find(db);
    find.prepare("SELECT id FROM songs WHERE path = ?");
    find.addBindValue(path);
    qint64 id = -1;
    if (find.exec() && find.next())
        id = find.value(0).toLongLong();
    else if (!hash.isEmpty())
    {
        find.prepare("SELECT id, path FROM songs WHERE hash = ?");
        find.addBindValue(hash);
        if (find.exec())
            while (find.next())
                if (!QFile::exists(find.value(1).toString()))
                {
                    id = find.value(0).toLongLong();
                    break;
                }
    }
    if (id < 0)
    {
        QSqlQuery insert(db);
        insert.prepare("INSERT INTO songs (path) VALUES (?)");
        insert.addBindValue(path);
        if (!insert.exec())
            return -1;
        id = insert.lastInsertId().toLongLong();
    }
    return id;
}

void CLibrary::songOpened(const QString &songPath, const CMidiInfo &info)
{
    const QString path = normalisePath(songPath);
    const QString hash = fileHash(path);
    const qint64 id = findOrCreateSong(path, hash);
    if (id < 0)
        return;

    QSqlQuery update(QSqlDatabase::database(m_connection));
    update.prepare("UPDATE songs SET path = ?, hash = ?, last_opened = ?, times_opened = times_opened + 1, "
                   "title = ?, time_sig_top = ?, time_sig_bottom = ?, time_sig_changes = ?, key_sharps = ?, "
                   "key_minor = ?, tempo_bpm = ?, tempo_changes = ?, duration_ms = ?, bars = ?, info_valid = ? "
                   "WHERE id = ?");
    update.addBindValue(path);
    update.addBindValue(hash);
    update.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    update.addBindValue(info.title);
    update.addBindValue(info.timeSigTop);
    update.addBindValue(info.timeSigBottom);
    update.addBindValue(info.timeSigChanges ? 1 : 0);
    update.addBindValue(info.keySharps);
    update.addBindValue(info.keyMinor ? 1 : 0);
    update.addBindValue(info.tempoBpm);
    update.addBindValue(info.tempoChanges ? 1 : 0);
    update.addBindValue(info.durationMs);
    update.addBindValue(info.bars);
    update.addBindValue(info.valid ? 1 : 0);
    update.addBindValue(id);
    if (!update.exec())
        ppLogError("Library database error: %s", qPrintable(update.lastError().text()));
}

void CLibrary::setFavourite(const QString &songPath, bool favourite)
{
    const QString path = normalisePath(songPath);
    QSqlDatabase db = QSqlDatabase::database(m_connection);
    QSqlQuery q(db);
    q.prepare("INSERT OR IGNORE INTO songs (path, hash) VALUES (?, ?)");
    q.addBindValue(path);
    q.addBindValue(fileHash(path));
    q.exec();
    q.prepare("UPDATE songs SET favourite = ? WHERE path = ?");
    q.addBindValue(favourite ? 1 : 0);
    q.addBindValue(path);
    q.exec();
}

void CLibrary::beginBatch()
{
    QSqlDatabase::database(m_connection).transaction();
}

void CLibrary::endBatch()
{
    QSqlDatabase::database(m_connection).commit();
}

void CLibrary::storeInfo(const QString &songPath, const CMidiInfo &info)
{
    const QString path = normalisePath(songPath);
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare("INSERT OR IGNORE INTO songs (path, hash) VALUES (?, ?)");
    q.addBindValue(path);
    q.addBindValue(fileHash(path));
    q.exec();
    q.prepare("UPDATE songs SET title = ?, time_sig_top = ?, time_sig_bottom = ?, time_sig_changes = ?, "
              "key_sharps = ?, key_minor = ?, tempo_bpm = ?, tempo_changes = ?, duration_ms = ?, bars = ?, "
              "info_valid = ? WHERE path = ?");
    q.addBindValue(info.title);
    q.addBindValue(info.timeSigTop);
    q.addBindValue(info.timeSigBottom);
    q.addBindValue(info.timeSigChanges ? 1 : 0);
    q.addBindValue(info.keySharps);
    q.addBindValue(info.keyMinor ? 1 : 0);
    q.addBindValue(info.tempoBpm);
    q.addBindValue(info.tempoChanges ? 1 : 0);
    q.addBindValue(info.durationMs);
    q.addBindValue(info.bars);
    q.addBindValue(info.valid ? 1 : 0);
    q.addBindValue(path);
    q.exec();
}

QVector<CLibrarySong> CLibrary::query(const QString &where, const QVariantList &values, const QString &tail) const
{
    QVector<CLibrarySong> result;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QString("SELECT %1, info_valid FROM songs %2 %3").arg(SONG_COLUMNS, where, tail));
    for (const QVariant &value : values)
        q.addBindValue(value);
    if (!q.exec())
    {
        ppLogError("Library database error: %s", qPrintable(q.lastError().text()));
        return result;
    }
    while (q.next())
    {
        CLibrarySong song;
        int c = 0;
        song.path = q.value(c++).toString();
        song.hash = q.value(c++).toString();
        song.favourite = q.value(c++).toInt() != 0;
        song.lastOpened = QDateTime::fromString(q.value(c++).toString(), Qt::ISODate);
        song.timesOpened = q.value(c++).toInt();
        song.info.title = q.value(c++).toString();
        song.info.timeSigTop = q.value(c++).toInt();
        song.info.timeSigBottom = q.value(c++).toInt();
        song.info.timeSigChanges = q.value(c++).toInt() != 0;
        song.info.keySharps = q.value(c).isNull() ? CMidiInfo::NO_KEY : q.value(c).toInt();
        c++;
        song.info.keyMinor = q.value(c++).toInt() != 0;
        song.info.tempoBpm = q.value(c++).toDouble();
        song.info.tempoChanges = q.value(c++).toInt() != 0;
        song.info.durationMs = q.value(c++).toLongLong();
        song.info.bars = q.value(c++).toInt();
        song.partCount = q.value(c++).toInt();
        song.info.valid = q.value(c++).toInt() != 0;
        result << song;
    }
    return result;
}

CLibrarySong CLibrary::song(const QString &path) const
{
    const QVector<CLibrarySong> found = query("WHERE path = ?", { normalisePath(path) });
    if (!found.isEmpty())
        return found.first();
    CLibrarySong unknown;
    unknown.path = normalisePath(path);
    return unknown;
}

QVector<CLibrarySong> CLibrary::songs(const QStringList &paths) const
{
    // read them all at once: much faster than one query per song for big folders
    QHash<QString, CLibrarySong> known;
    for (const CLibrarySong &song : query(QString()))
        known.insert(song.path, song);

    QVector<CLibrarySong> result;
    for (const QString &path : paths)
    {
        const QString normalised = normalisePath(path);
        if (known.contains(normalised))
            result << known.value(normalised);
        else
        {
            CLibrarySong unknown;
            unknown.path = normalised;
            result << unknown;
        }
    }
    return result;
}

QVector<CLibrarySong> CLibrary::favourites() const
{
    return query("WHERE favourite = 1", {}, "ORDER BY path COLLATE NOCASE");
}

QVector<CLibrarySong> CLibrary::recent(int maximum) const
{
    return query("WHERE last_opened IS NOT NULL", {}, QString("ORDER BY last_opened DESC LIMIT %1").arg(maximum));
}

qint64 CLibrary::songId(const QString &songPath, bool create) const
{
    const QString path = normalisePath(songPath);
    QSqlQuery q(QSqlDatabase::database(m_connection));
    if (create)
    {
        q.prepare("INSERT OR IGNORE INTO songs (path, hash) VALUES (?, ?)");
        q.addBindValue(path);
        q.addBindValue(fileHash(path));
        q.exec();
    }
    q.prepare("SELECT id FROM songs WHERE path = ?");
    q.addBindValue(path);
    if (q.exec() && q.next())
        return q.value(0).toLongLong();
    return -1;
}

QVector<CSongPart> CLibrary::parts(const QString &songPath) const
{
    QVector<CSongPart> result;
    const qint64 id = songId(songPath, false);
    if (id < 0)
        return result;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare("SELECT id, name, start_bar, start_beat, start_ticks, end_bar, end_beat, end_ticks "
              "FROM parts WHERE song_id = ? ORDER BY id");
    q.addBindValue(id);
    if (!q.exec())
        return result;
    while (q.next())
    {
        CSongPart part;
        part.id = q.value(0).toLongLong();
        part.name = q.value(1).toString();
        part.start = { q.value(2).toInt(), q.value(3).toInt(), q.value(4).toLongLong() };
        part.end = { q.value(5).toInt(), q.value(6).toInt(), q.value(7).toLongLong() };
        result << part;
    }
    return result;
}

qint64 CLibrary::addPart(const QString &songPath, const QString &name, const CBarPosition &start, const CBarPosition &end)
{
    const qint64 id = songId(songPath, true);
    if (id < 0)
        return -1;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare("INSERT INTO parts (song_id, name, start_bar, start_beat, start_ticks, end_bar, end_beat, end_ticks) "
              "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    for (const QVariant &value : QVariantList{ id, name, start.bar, start.beat, start.ticks, end.bar, end.beat, end.ticks })
        q.addBindValue(value);
    if (!q.exec())
    {
        ppLogError("Library database error: %s", qPrintable(q.lastError().text()));
        return -1;
    }
    return q.lastInsertId().toLongLong();
}

void CLibrary::updatePart(const CSongPart &part)
{
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare("UPDATE parts SET name = ?, start_bar = ?, start_beat = ?, start_ticks = ?, "
              "end_bar = ?, end_beat = ?, end_ticks = ? WHERE id = ?");
    for (const QVariant &value : QVariantList{ part.name, part.start.bar, part.start.beat, part.start.ticks,
                                               part.end.bar, part.end.beat, part.end.ticks, part.id })
        q.addBindValue(value);
    if (!q.exec())
        ppLogError("Library database error: %s", qPrintable(q.lastError().text()));
}

void CLibrary::deletePart(qint64 id)
{
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare("DELETE FROM parts WHERE id = ?");
    q.addBindValue(id);
    q.exec();
}

CSongSettings CLibrary::songSettings(const QString &songPath) const
{
    CSongSettings settings;
    const QString path = normalisePath(songPath);
    const QString hash = fileHash(path);
    QSqlQuery q(QSqlDatabase::database(m_connection));
    // its own settings first, or else those of a copy of the song
    q.prepare("SELECT hand, speed, left_channel, right_channel FROM songs "
              "WHERE hand IS NOT NULL AND (path = ? OR (hash = ? AND hash <> '')) "
              "ORDER BY path = ? DESC LIMIT 1");
    q.addBindValue(path);
    q.addBindValue(hash);
    q.addBindValue(path);
    if (q.exec() && q.next())
    {
        settings.valid = true;
        settings.hand = q.value(0).toString();
        settings.speed = q.value(1).isNull() ? 100 : q.value(1).toInt();
        settings.leftChannel = q.value(2).isNull() ? -2 : q.value(2).toInt();
        settings.rightChannel = q.value(3).isNull() ? -2 : q.value(3).toInt();
    }
    return settings;
}

void CLibrary::setSongSettings(const QString &songPath, const CSongSettings &settings)
{
    const QString path = normalisePath(songPath);
    const QString hash = fileHash(path);
    const qint64 id = findOrCreateSong(path, hash);
    if (id < 0)
        return;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare("UPDATE songs SET path = ?, hash = ? WHERE id = ?");
    q.addBindValue(path);
    q.addBindValue(hash);
    q.addBindValue(id);
    q.exec();

    // the song and all its copies
    q.prepare("UPDATE songs SET hand = ?, speed = ?, left_channel = ?, right_channel = ? "
              "WHERE id = ? OR (hash = ? AND hash <> '')");
    q.addBindValue(settings.hand);
    q.addBindValue(settings.speed);
    q.addBindValue(settings.leftChannel == -2 ? QVariant() : QVariant(settings.leftChannel));
    q.addBindValue(settings.rightChannel == -2 ? QVariant() : QVariant(settings.rightChannel));
    q.addBindValue(id);
    q.addBindValue(hash);
    if (!q.exec())
        ppLogError("Library database error: %s", qPrintable(q.lastError().text()));
}

QString CLibrary::lastSong(const QString &folder) const
{
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare("SELECT last_song FROM folders WHERE path = ?");
    q.addBindValue(normalisePath(folder));
    if (q.exec() && q.next())
        return q.value(0).toString();
    return QString();
}

void CLibrary::setLastSong(const QString &folder, const QString &songName)
{
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare("INSERT OR REPLACE INTO folders (path, last_song) VALUES (?, ?)");
    q.addBindValue(normalisePath(folder));
    q.addBindValue(songName);
    if (!q.exec())
        ppLogError("Library database error: %s", qPrintable(q.lastError().text()));
}
