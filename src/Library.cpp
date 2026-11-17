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
        "time_sig_changes, key_sharps, key_minor, tempo_bpm, tempo_changes, duration_ms, bars";
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
             exec("PRAGMA user_version = 1");
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

void CLibrary::songOpened(const QString &songPath, const CMidiInfo &info)
{
    const QString path = normalisePath(songPath);
    const QString hash = fileHash(path);
    QSqlDatabase db = QSqlDatabase::database(m_connection);

    // Find the song by its path, or else by its contents if it has been moved or renamed
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
            return;
        id = insert.lastInsertId().toLongLong();
    }

    QSqlQuery update(db);
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
