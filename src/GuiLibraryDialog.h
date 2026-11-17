/*********************************************************************************/
/*!
@file           GuiLibraryDialog.h

@brief          The song library window: favourites, recent songs and the MIDI files
                in the locations (folders) chosen by the user.

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

#ifndef __GUI_LIBRARY_DIALOG_H__
#define __GUI_LIBRARY_DIALOG_H__

#include <QDialog>

#include "Library.h"

class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class GuiLibraryDialog : public QDialog
{
    Q_OBJECT

public:
    GuiLibraryDialog(CLibrary *library, const QString &currentSong, QWidget *parent = nullptr);

    // the song to open, after the dialog has been accepted
    QString selectedSong() const { return m_selectedSong; }

private slots:
    void sourceChanged();
    void sourceExpanded(QTreeWidgetItem *item);
    void songClicked(QTreeWidgetItem *item, int column);
    void filterSongs();
    void openSelected();
    void addLocation();
    void removeLocation();
    void updateButtons();

private:
    enum SourceKind { SOURCE_FAVOURITES, SOURCE_RECENT, SOURCE_LOCATION, SOURCE_FOLDER };

    void fillSources(const QString &locationToSelect = QString());
    void addSubfolders(QTreeWidgetItem *item);
    void showSongs(const QVector<CLibrarySong> &songs, const QString &baseFolder);

    CLibrary *m_library;
    QString m_currentSong;
    QString m_selectedSong;

    QTreeWidget *m_sources;
    QTreeWidget *m_songs;
    QLineEdit *m_search;
    QPushButton *m_openButton;
    QPushButton *m_removeLocationButton;
};

#endif // __GUI_LIBRARY_DIALOG_H__
