/*********************************************************************************/
/*!
@file           GuiSectionEditor.h

@brief          The section editor: a window where the named sections of a song are
                created and changed on a piano roll of the whole song, which can be
                scrolled to any place without playing it, and where they can be heard.

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

#ifndef __GUI_SECTION_EDITOR_H__
#define __GUI_SECTION_EDITOR_H__

#include <QDialog>

#include "Library.h"
#include "SongMap.h"

class CMidiDevice;
class CPianoRollView;
class CSongOverview;
class CSectionPlayer;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;

class GuiSectionEditor : public QDialog
{
    Q_OBJECT

public:
    // The song must be the one loaded in the practice window, whose conductor must be
    // stopped while the editor is open, because the editor plays through the same device.
    // The instruments are the items of the list of the practice window (their names and
    // MIDI channels), and activeChannel the one chosen there.
    GuiSectionEditor(CLibrary *library, const QString &songPath, const QString &songTitle,
                     CMidiDevice *device, double speed,
                     const QStringList &instrumentNames, const QVector<int> &instrumentChannels, int activeChannel,
                     QWidget *parent = nullptr);
    ~GuiSectionEditor() override;

    bool isValid() const { return m_map.isValid(); }

    // What is selected when the editor opens: a saved section, or else the start and
    // end set in the practice window (if the loop is active).
    void setInitialSelection(qint64 partId, bool loopActive, const CBarPosition &loopStart, const CBarPosition &loopEnd);

    // After the dialog is accepted with "Practise": the saved section to practise, or
    // (partId < 0) the start and the end of a selection that was not saved.
    qint64 practisePartId() const { return m_practisePartId; }
    CBarPosition practiseStart() const { return m_practiseStart; }
    CBarPosition practiseEnd() const { return m_practiseEnd; }
    // The MIDI channel of the instrument chosen in the editor (-1 if there is no list)
    int practiseChannel() const;

public slots:
    void reject() override;

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void partRowChanged(int row);
    void partClicked(qint64 id);
    void rollSelectionChanged(qint64 start, qint64 end);
    void rollNewSelection();
    void startEdited();
    void endEdited();
    void newPart();
    void savePart();
    void renamePart();
    void deletePart();
    void togglePlay();
    void playerPosition(qint64 tick);
    void playerStopped();
    void practise();
    void updateOverviewRange();

private:
    void loadParts(qint64 selectId);
    void setSelection(qint64 start, qint64 end);
    void showSelectionInFields();
    void updateInfo();
    void updateButtons();
    const CSongPart *currentPart() const;
    bool isDirty() const;
    // Asks what to do with the changes to the current section. Returns false to cancel.
    bool resolveChanges();
    QString partRangeText(const CSongPart &part) const;
    QString rangeText(qint64 start, qint64 end) const;

    CLibrary *m_library;
    QString m_songPath;
    CMidiDevice *m_device;
    CSongMap m_map;
    QVector<CSongPart> m_parts;
    qint64 m_currentPartId = -1;
    qint64 m_selStart = 0;
    qint64 m_selEnd = 0;
    bool m_firstShow = true;

    qint64 m_practisePartId = -1;
    CBarPosition m_practiseStart;
    CBarPosition m_practiseEnd;

    CSectionPlayer *m_player;
    CPianoRollView *m_roll;
    CSongOverview *m_overview;
    QComboBox *m_snapCombo;
    QComboBox *m_instrumentCombo;
    QCheckBox *m_otherPartsCheck;
    QListWidget *m_partList;
    QPushButton *m_newButton;
    QPushButton *m_saveButton;
    QPushButton *m_renameButton;
    QPushButton *m_deleteButton;
    QSpinBox *m_startBarSpin;
    QDoubleSpinBox *m_startBeatSpin;
    QSpinBox *m_endBarSpin;
    QDoubleSpinBox *m_endBeatSpin;
    QLabel *m_infoLabel;
    QPushButton *m_playButton;
    QCheckBox *m_repeatCheck;
    QSpinBox *m_speedSpin;
    QPushButton *m_practiseButton;
};

#endif // __GUI_SECTION_EDITOR_H__
