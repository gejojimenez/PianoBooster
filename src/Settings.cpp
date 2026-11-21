/*!
    @file           Settings.cpp

    @brief          Save all the settings for the programme in the right place.

    @author         L. J. Barman

    Copyright (c)   2008-2020, L. J. Barman and others, all rights reserved

    This file is part of the PianoBooster application

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

/*!
 * The settings of each song (hand, speed and the MIDI channel of each hand) and the
 * last song of each folder (book) are kept in the library database (see Library.h).
*/

#include <QFile>
#include <QXmlStreamReader>
#include "Settings.h"
#include "GuiTopBar.h"
#include "GuiSidePanel.h"
#include "QtWindow.h"
#include "version.h"

#if WITH_INTERNAL_FLUIDSYNTH
#include "MidiDeviceFluidSynth.h"
#endif

#define OPTION_DEBUG_SETTINGS   0
#if OPTION_DEBUG_SETTINGS
#define debugSettings(args)     qDebug args
#else
#define debugSettings(args)
#endif

CSettings::CSettings(QtWindow *mainWindow) : QSettings(CSettings::IniFormat, CSettings::UserScope, "PianoBooster", "Piano Booster"),
                                 m_mainWindow(mainWindow)
{
    // It is all done in the initialisation list

    m_advancedMode = false;
    m_noteNamesEnabled = value("Score/NoteNames", true ).toBool();
    m_coloredNotes = value("Score/ColoredNotes", false ).toBool();
    m_tutorPagesEnabled = value("Tutor/TutorPages", true ).toBool();
    CNotation::setCourtesyAccidentals(value("Score/CourtesyAccidentals", false ).toBool());
    m_followThroughErrorsEnabled = value("Score/FollowThroughErrors", false ).toBool();
    m_noteDurationTrail = value("Score/NoteDurationTrail", false ).toBool();

    // load Fluid settings
    setFluidSoundFontNames( value("FluidSynth/SoundFont").toStringList());
}

void CSettings::setDefaultValue(const QString & key, const QVariant & value )
{
    if (contains(key)) // Do not change the value if it already set
        return;

    setValue(key, value);
}

void CSettings::init(CSong* song, GuiSidePanel* sidePanel, GuiTopBar* topBar)
{
    m_song = song;
    m_guiSidePanel = sidePanel;
    m_guiTopBar = topBar;
}

void CSettings::setNoteNamesEnabled(bool value) {
    m_noteNamesEnabled = value;
    setValue("Score/NoteNames", value );
}

void CSettings::setColoredNotes(bool value) {
    m_coloredNotes = value;
    setValue("Score/ColoredNotes", value );
}

void CSettings::setTutorPagesEnabled(bool value) {
    m_tutorPagesEnabled = value;
    setValue("Tutor/TutorPages", value );
    updateTutorPage();
}

void CSettings::setCourtesyAccidentals(bool value) {
    CNotation::setCourtesyAccidentals(value);
    setValue("Score/CourtesyAccidentals", value );
}

void CSettings::setFollowThroughErrorsEnabled(bool value) {
    m_followThroughErrorsEnabled = value;
    setValue("Score/FollowThroughErrors", value );
}

void CSettings::setNoteDurationTrail(bool value) {
    m_noteDurationTrail = value;
    setValue("Score/NoteDurationTrail", value );
}

void CSettings::loadSongSettings()
{
    m_songSettingsPath = getCurrentSongLongFileName();
    m_songSettings = (m_library && !m_songSettingsPath.isEmpty()) ? m_library->songSettings(m_songSettingsPath)
                                                                  : CSongSettings();
    m_guiSidePanel->setCurrentHand(m_songSettings.hand);
    m_guiTopBar->setSpeed(m_songSettings.speed);
    CNote::setChannelHands(m_songSettings.leftChannel, m_songSettings.rightChannel);
}

// Saves the hand and the speed of the song whose settings were loaded last, if they have changed
void CSettings::saveSongSettings()
{
    if (!m_library || m_songSettingsPath.isEmpty() || !QFile::exists(m_songSettingsPath))
        return;
    const QString hand = partToHandString(m_song->getActiveHand());
    const int speed = m_guiTopBar->getSpeed();
    if (m_songSettings.valid && hand == m_songSettings.hand && speed == m_songSettings.speed)
        return;
    if (!m_songSettings.valid && hand == "both" && speed == 100)
        return; // still the defaults: nothing to keep
    m_songSettings.valid = true;
    m_songSettings.hand = hand;
    m_songSettings.speed = speed;
    m_library->setSongSettings(m_songSettingsPath, m_songSettings);
}

void CSettings::loadBookSettings()
{
    if (!m_library)
        return;
    importBookConfig(currentBookFolder());
    QString lastSong = m_library->lastSong(currentBookFolder());
    if (!lastSong.isEmpty() && m_currentSongName.isEmpty())
        m_currentSongName = lastSong;
}

// Older versions kept the settings of the songs of each folder in a "pb.cfg" file
// in the folder: move them to the library and delete the file.
void CSettings::importBookConfig(const QString &folder)
{
    QFile file(folder + "/pb.cfg");
    if (!file.open(QIODevice::ReadOnly))
        return;

    m_library->beginBatch();
    QXmlStreamReader xml(&file);
    while (!xml.atEnd())
    {
        if (xml.readNext() != QXmlStreamReader::StartElement)
            continue;
        const QXmlStreamAttributes attributes = xml.attributes();
        if (xml.name() == QLatin1String("book"))
        {
            const QString lastSong = attributes.value("lastsong").toString();
            if (!lastSong.isEmpty() && m_library->lastSong(folder).isEmpty())
                m_library->setLastSong(folder, lastSong);
        }
        else if (xml.name() == QLatin1String("song"))
        {
            const QString path = folder + '/' + attributes.value("name").toString();
            // the settings already in the library, maybe of a copy of the song, are kept
            if (!attributes.value("name").isEmpty() && QFile::exists(path) && !m_library->songSettings(path).valid)
            {
                CSongSettings settings;
                settings.valid = true;
                if (attributes.hasAttribute("hand"))
                    settings.hand = attributes.value("hand").toString();
                if (attributes.hasAttribute("speed"))
                    settings.speed = attributes.value("speed").toInt();
                if (attributes.hasAttribute("leftHandMidiChannel"))
                    settings.leftChannel = attributes.value("leftHandMidiChannel").toInt();
                if (attributes.hasAttribute("rightHandMidiChannel"))
                    settings.rightChannel = attributes.value("rightHandMidiChannel").toInt();
                m_library->setSongSettings(path, settings);
            }
        }
    }
    m_library->endBatch();
    file.close();
    if (!file.remove())
        ppLogError("Cannot delete %s", qPrintable(file.fileName()));
}

void CSettings::updateTutorPage()
{
    QFileInfo fileInfo(getCurrentSongLongFileName());
    const char* EXTN = ".html";



    if (m_tutorPagesEnabled)
    {
        QString locale = selectedLangauge();

        QString tutorFilePath = fileInfo.absolutePath() + "/InfoPages/" + locale + "/";
        QFileInfo tutorFile;
        tutorFile.setFile(tutorFilePath + fileInfo.completeBaseName() + EXTN);
        if (tutorFile.exists())
        {
            m_mainWindow->loadTutorHtml(tutorFile.absoluteFilePath());
            return;
        }

        tutorFilePath = fileInfo.absolutePath() + "/InfoPages/en/";
        tutorFile.setFile(tutorFilePath + fileInfo.completeBaseName() + EXTN);

        if (tutorFile.exists())
        {
            m_mainWindow->loadTutorHtml(tutorFile.absoluteFilePath());
            return;
        }

        // The previous location of the tutor pages English only
        tutorFile.setFile(fileInfo.absolutePath() + "/InfoPages/" + fileInfo.completeBaseName() + "_en" + EXTN);
        if (tutorFile.exists())
        {
            m_mainWindow->loadTutorHtml(tutorFile.absoluteFilePath());
            return;
        }
    }
    m_mainWindow->loadTutorHtml(QString());
}

void CSettings::openSongFile(const QString & filename)
{
    if (!QFile::exists(filename))
    {
        ppLogError( "File does not exists %s", qPrintable(filename));
        return;
    }
    QDir dirBooks;
    QString  currentSongName;
    if (filename.isEmpty())
    {
        dirBooks.setPath( QDir::homePath());
    }
    else
    {
        dirBooks.setPath(filename);
        currentSongName = dirBooks.dirName();
        dirBooks.cdUp();
        m_currentBookName = dirBooks.dirName();
        dirBooks.cdUp();
    }
    m_bookPath =  dirBooks.path() + '/';

    m_currentSongName = currentSongName;
    m_guiSidePanel->loadBookList();
    updateWarningMessages();
}

void CSettings::setActiveHand(whichPart_t hand)
{
    m_song->setActiveHand(hand);
}

QStringList CSettings::getSongList()
{
    debugSettings(("getSongList %s + %s", qPrintable(getCurrentBookName()), qPrintable(m_bookPath)));
    QDir dirSongs = QDir(m_bookPath + getCurrentBookName());
    dirSongs.setFilter(QDir::Files);
    QStringList fileNames = dirSongs.entryList();

    QStringList songNames;
    for (int i = 0; i < fileNames.size(); i++)
    {
        if ( fileNames.at(i).endsWith(".mid", Qt::CaseInsensitive ) ||
             fileNames.at(i).endsWith(".midi", Qt::CaseInsensitive ) ||
             fileNames.at(i).endsWith(".kar", Qt::CaseInsensitive ) )
        {
            songNames  +=  fileNames.at(i);
        }
    }

    return songNames;
}

QStringList CSettings::getBookList()
{
    QDir dirBooks( m_bookPath);
    dirBooks.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
    return dirBooks.entryList();
}

void CSettings::writeSettings()
{

    if (QFile::exists(getCurrentSongLongFileName() ))
        setValue("CurrentSong", getCurrentSongLongFileName());
    saveSongSettings();
}

void CSettings::loadSettings()
{
    unzipBoosterMusicBooks();
    // Set default values
    setValue("PianoBooster/Version", PB_VERSION);
    setDefaultValue("ShortCuts/LeftHand", "F2");
    setDefaultValue("ShortCuts/BothHands","F3");
    setDefaultValue("ShortCuts/RightHand","F4");
    setDefaultValue("ShortCuts/PlayFromStart",tr("space"));
    setDefaultValue("ShortCuts/PlayPause","P");
    setDefaultValue("ShortCuts/Faster","=");
    setDefaultValue("ShortCuts/Slower","-");
    setDefaultValue("ShortCuts/NextSong","]");
    setDefaultValue("ShortCuts/PreviousSong","[");
    setDefaultValue("ShortCuts/NextBook","{");
    setDefaultValue("ShortCuts/PreviousBook","}");
//    QString songName = value("CurrentSong").toString();
//    if (!songName.isEmpty())
//        openSongFile( songName );

    setupDefaultSoundFont();

    updateWarningMessages();
    updateTutorPage();

}

QString CSettings::boosterMusicBooksFolder()
{
#ifdef _WIN32
    QSettings settings(QSettings::UserScope, "Microsoft", "Windows");
    settings.beginGroup("CurrentVersion/Explorer/Shell Folders");
    const QString musicDir = QDir::fromNativeSeparators(settings.value("Personal").toString()) + "/My Music";
#else
    const QString musicDir = QDir::homePath() + "/Music";
#endif
    return musicDir + "/BoosterMusicBooks" + QString::number(MUSIC_RELEASE);
}

void CSettings::unzipBoosterMusicBooks()
{
    // Set default value
    const QString ZIPFILENAME("BoosterMusicBooks.zip");

    if (value("PianoBooster/MusicRelease", 0).toInt() < MUSIC_RELEASE)
    {
        QString musicSrcDir = QApplication::applicationDirPath() + "/";

        ppLogTrace("unzipBoosterMusicBooks resourceDir1 %s", qPrintable(musicSrcDir));

        if (!QFile::exists(musicSrcDir + ZIPFILENAME))
        {
#if defined (Q_OS_LINUX) || defined (Q_OS_UNIX)
            musicSrcDir=Util::dataDir()+"/music/";
#endif
#ifdef Q_OS_DARWIN
            musicSrcDir = QApplication::applicationDirPath() + "/../Resources/music/";
#endif
        }

        ppLogInfo(qPrintable("applicationDirPath=" + QApplication::applicationDirPath()));
        ppLogTrace("resourceDir %s", qPrintable(musicSrcDir));

        QFileInfo zipFile(musicSrcDir +  ZIPFILENAME);

        QDir destMusicDir;

#ifdef _WIN32
        const QString MUSIC_DIR_NAME("My Music");
        QSettings settings(QSettings::UserScope, "Microsoft", "Windows");
        settings.beginGroup("CurrentVersion/Explorer/Shell Folders");
        destMusicDir.setPath(QDir::fromNativeSeparators(settings.value("Personal").toString()));
#else
        const QString MUSIC_DIR_NAME("Music");
        destMusicDir.setPath(QDir::homePath() );
#endif

        if (!QDir(destMusicDir.absolutePath() + "/" + MUSIC_DIR_NAME).exists())
        {
            destMusicDir.mkdir(MUSIC_DIR_NAME);
        }
        destMusicDir.setPath(destMusicDir.absolutePath() + "/" + MUSIC_DIR_NAME);

#ifndef _WIN32

        // on windows the installer does the unzipping
        if (!zipFile.exists() )
        {
            ppLogError(qPrintable("Cannot find " + ZIPFILENAME));
            return;
        }

        QProcess unzip;
        unzip.start("unzip", QStringList() << "-o" << zipFile.filePath() << "-d" << destMusicDir.path() );
        ppLogInfo(qPrintable("running unzip -o " + zipFile.filePath() + " -d " + destMusicDir.path()) );
        char buf[1024];
        while (true)
        {
            qint64 lineLength = unzip.readLine(buf, sizeof(buf));
            if (lineLength <= 0)
                break;
            ppLogInfo(buf);
                 // the line is available in buf
        }

        if (!unzip.waitForFinished())
        {
             ppLogError("unzip failed");
             return;
        }
#endif
        QString fileName(destMusicDir.absolutePath() + "/BoosterMusicBooks" + QString::number(MUSIC_RELEASE) + "/Beginner Course/01-StartWithMiddleC.mid");
        openSongFile(fileName);
        m_mainWindow->setCurrentFile(fileName);
        setValue("PianoBooster/MusicRelease", MUSIC_RELEASE);
    }
}

void CSettings::setCurrentSongName(const QString & name)
{
    if (name.isEmpty())
        return;
    saveSongSettings();
    m_currentSongName = name;
    debugSettings(("setCurrentSongName %s -- %s", qPrintable(name), qPrintable(getCurrentSongLongFileName())));
    setValue("CurrentSong", getCurrentSongLongFileName());
    if (m_library)
        m_library->setLastSong(currentBookFolder(), m_currentSongName);

    m_song->loadSong(getCurrentSongLongFileName());
    loadSongSettings();

    m_guiSidePanel->refresh();
    m_guiTopBar->refresh(true);
    m_mainWindow->songLoaded(getCurrentSongLongFileName()); // title, song details and library
    updateTutorPage();
}

void CSettings::setCurrentBookName(const QString & name, bool clearSongName)
{
    if (name.isEmpty())
        return;
    saveSongSettings();

    m_currentBookName = name;
    if (clearSongName)
        m_currentSongName.clear();
    debugSettings(("setCurrentBookName %s --- %s ", qPrintable(name), qPrintable(getCurrentSongLongFileName())));
    loadBookSettings();
}

void CSettings::setChannelHands(int left, int right)
{
    CNote::setChannelHands(left, right);
    if (m_library && !m_songSettingsPath.isEmpty() && QFile::exists(m_songSettingsPath))
    {
        m_songSettings.valid = true;
        m_songSettings.hand = partToHandString(m_song->getActiveHand());
        m_songSettings.speed = m_guiTopBar->getSpeed();
        m_songSettings.leftChannel = left;
        m_songSettings.rightChannel = right;
        m_library->setSongSettings(m_songSettingsPath, m_songSettings);
    }
    m_guiSidePanel->refresh();
}

void CSettings::updateWarningMessages()
{
    if (!m_song->validMidiOutput())
        m_warningMessage = tr("ERROR NO SOUND: To fix this use menu Setup/MIDI Setup ...");
    else if (m_currentSongName.isEmpty())
        m_warningMessage = tr("ERROR NO MIDI FILE: To fix this use menu File/Open ...");
    else
        m_warningMessage.clear();
}

void CSettings::setupDefaultSoundFont(){
#if WITH_INTERNAL_FLUIDSYNTH

    QFileInfo defaultSoundFont = QFileInfo();

    if (getFluidSoundFontNames().empty() && !m_song->validMidiOutput() && !contains("LastSoundFontDir"))
    {
        QString appPath = qgetenv("APPIMAGE");
        if (!appPath.isEmpty())
        {
             appPath= QFileInfo(appPath).path();
        }
        else {
            appPath = QApplication::applicationDirPath();
        }

        QDir directory(appPath);
        directory.cd("soundfont");
        QStringList dirList = directory.entryList(QStringList(),QDir::AllEntries);
        foreach(QString filename, dirList)
        {
            // Find the first sound font file
            if ( filename.endsWith(".sf2", Qt::CaseInsensitive ) )
            {
                defaultSoundFont.setFile(directory.path(), filename);
                break;
            }
        }

#if defined (Q_OS_LINUX) || defined (Q_OS_UNIX)
        if (!defaultSoundFont.isFile())
        {
            QStringList possibleSoundFontFolders = {"/usr/share/soundfonts","/usr/share/sounds/sf2"};
            for (QString soundFontFolder:possibleSoundFontFolders)
            {
                // fluid-soundfont-gm Fluid (R3) General MIDI SoundFont (GM) package
                QFileInfo foundSoundFont(soundFontFolder, "FluidR3_GM.sf2");
                if (foundSoundFont.isFile())
                {
                       defaultSoundFont=foundSoundFont;
                       break;
                }
            }
        }
#endif

        if (defaultSoundFont.isFile()) {
            setFluidSoundFontNames(defaultSoundFont.filePath());
            setValue("Midi/Output",CMidiDeviceFluidSynth::getFluidInternalName() );
            setValue("LastSoundFontDir", defaultSoundFont.path());
            saveSoundFontSettings();
            m_song->openMidiPort(CMidiDevice::MIDI_OUTPUT, CMidiDeviceFluidSynth::getFluidInternalName());
        }
    }
#endif
}
