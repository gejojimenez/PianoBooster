/*********************************************************************************/
/*!
@file           GuiSidePanel.cpp

@brief          xxxx.

@author         L. J. Barman

    Copyright (c)   2008-2013, L. J. Barman, all rights reserved

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
/*********************************************************************************/

#include <QtWidgets>

#include "GuiSidePanel.h"
#include "GuiTopBar.h"
#include "TrackList.h"
#include "Conductor.h"

GuiSidePanel::GuiSidePanel(QWidget *parent, CSettings* settings)
    : QWidget(parent), m_parent(parent)
{
    m_song = nullptr;
    m_score = nullptr;
    m_trackList = nullptr;
    m_topBar = nullptr;
    m_settings = settings;
    setupUi(this);

    // The song header replaces the book and song lists, which stay hidden because
    // the next/previous song and book shortcuts still use them.
    for (QWidget *widget : { static_cast<QWidget*>(label), static_cast<QWidget*>(bookCombo),
                             static_cast<QWidget*>(label_2), static_cast<QWidget*>(songCombo) })
        widget->hide();

    m_songTitleLabel = new QLabel;
    QFont titleFont = m_songTitleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.15);
    m_songTitleLabel->setFont(titleFont);
    m_songTitleLabel->setWordWrap(true);
    m_songFolderLabel = new QLabel;
    m_songInfoLabel = new QLabel;
    m_songInfoLabel->setWordWrap(true);
    m_libraryButton = new QPushButton(tr("Library..."));
    m_libraryButton->setToolTip(tr("Choose a song from the library: favourites, recent songs and your folders"));
    connect(m_libraryButton, &QPushButton::clicked, this, &GuiSidePanel::libraryRequested);

    // the named parts of the song
    m_partLabel = new QLabel(tr("Section:"));
    m_partCombo = new QComboBox;
    m_partCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_partCombo->setMinimumContentsLength(10);
    m_partCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_addPartButton = new QToolButton;
    m_addPartButton->setText("+");
    m_updatePartButton = new QToolButton;
    m_updatePartButton->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    m_renamePartButton = new QToolButton;
    m_renamePartButton->setText(QString::fromUtf8("✎"));
    m_deletePartButton = new QToolButton;
    m_deletePartButton->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    connect(m_partCombo, QOverload<int>::of(&QComboBox::activated), this, &GuiSidePanel::partSelected);
    connect(m_addPartButton, &QToolButton::clicked, this, &GuiSidePanel::addPart);
    connect(m_updatePartButton, &QToolButton::clicked, this, &GuiSidePanel::updatePart);
    connect(m_renamePartButton, &QToolButton::clicked, this, &GuiSidePanel::renamePart);
    connect(m_deletePartButton, &QToolButton::clicked, this, &GuiSidePanel::deletePart);
    // the list on one row and its buttons below, so that the list is wide enough for the names
    auto *partListLayout = new QHBoxLayout;
    partListLayout->addWidget(m_partLabel);
    partListLayout->addWidget(m_partCombo, 1);
    auto *partButtonLayout = new QHBoxLayout;
    partButtonLayout->addStretch(1);
    for (QToolButton *button : { m_addPartButton, m_updatePartButton, m_renamePartButton, m_deletePartButton })
    {
        button->setAutoRaise(true);
        partButtonLayout->addWidget(button);
    }
    auto *partLayout = new QVBoxLayout;
    partLayout->setSpacing(0);
    partLayout->addLayout(partListLayout);
    partLayout->addLayout(partButtonLayout);
    setPartTexts();

    auto *headerLayout = new QVBoxLayout;
    headerLayout->setContentsMargins(0, 0, 0, 4);
    headerLayout->addWidget(m_songTitleLabel);
    headerLayout->addWidget(m_songFolderLabel);
    headerLayout->addWidget(m_songInfoLabel);
    headerLayout->addWidget(m_libraryButton);
    headerLayout->addLayout(partLayout);
    if (auto *mainLayout = qobject_cast<QBoxLayout*>(layout()))
        mainLayout->insertLayout(0, headerLayout);

    setSongInfo(QString(), QString(), CMidiInfo());
}

void GuiSidePanel::setSongInfo(const QString &title, const QString &fileName, const CMidiInfo &info)
{
    m_songTitle = title;
    m_songFileName = fileName;
    m_songInfo = info;
    m_songTitleLabel->setText(fileName.isEmpty() ? tr("No song") : title);

    const QFileInfo file(fileName);
    m_songFolderLabel->setText(fileName.isEmpty() ? QString() : QString::fromUtf8("\U0001F4C1 ") + file.dir().dirName());
    m_songFolderLabel->setToolTip(QDir::toNativeSeparators(fileName));
    m_songTitleLabel->setToolTip(QDir::toNativeSeparators(fileName));

    if (!info.valid)
    {
        m_songInfoLabel->clear();
        return;
    }
    QStringList first = { info.timeSigText() };
    if (!info.keyText().isEmpty())
        first << info.keyText();
    first << info.tempoText();
    const QString second = info.durationText() + QString::fromUtf8(" · ") + tr("%n bar(s)", nullptr, info.bars);
    m_songInfoLabel->setText(first.join(QString::fromUtf8(" · ")) + "\n" + second);
}

void GuiSidePanel::setPartTexts()
{
    m_partLabel->setText(tr("Section:"));
    m_partCombo->setToolTip(tr("Practise a section of the song: it is played from its start to its end, repeating"));
    m_addPartButton->setToolTip(tr("Save the current start and end (Start / Repeat) as a new section"));
    m_updatePartButton->setToolTip(tr("Replace the start and end of the selected section with the current ones"));
    m_renamePartButton->setToolTip(tr("Rename the selected section"));
    m_deletePartButton->setToolTip(tr("Delete the selected section"));
    if (m_partCombo->count() > 0)
        m_partCombo->setItemText(0, tr("Whole song"));
}

// Reads the parts of the current song from the library
void GuiSidePanel::loadParts(qint64 selectId)
{

    m_parts = (m_library && !m_songFileName.isEmpty()) ? m_library->parts(m_songFileName) : QVector<CSongPart>();

    const QSignalBlocker block(m_partCombo);
    m_partCombo->clear();
    m_partCombo->addItem(tr("Whole song"), -1);
    for (const CSongPart &part : m_parts)
    {
        m_partCombo->addItem(part.name, part.id);
        if (part.id == selectId)
            m_partCombo->setCurrentIndex(m_partCombo->count() - 1);
    }
    updatePartButtons();
}

void GuiSidePanel::updatePartButtons()
{
    const bool hasSong = m_library && !m_songFileName.isEmpty();
    const bool partSelected = m_partCombo->currentIndex() > 0;
    m_partCombo->setEnabled(hasSong);
    m_addPartButton->setEnabled(hasSong);
    m_updatePartButton->setEnabled(partSelected);
    m_renamePartButton->setEnabled(partSelected);
    m_deletePartButton->setEnabled(partSelected);
}

const CSongPart *GuiSidePanel::selectedPart() const
{
    const qint64 id = m_partCombo->currentData().toLongLong();
    for (const CSongPart &part : m_parts)
        if (part.id == id)
            return &part;
    return nullptr;
}

// Plays the selected part, or the whole song, from now on
void GuiSidePanel::partSelected(int index)
{
    Q_UNUSED(index)
    if (!m_song)
        return;
    if (m_topBar)
        m_topBar->stopMuiscPlaying();

    if (const CSongPart *part = selectedPart())
    {
        m_song->setPlayFrom(part->start);
        m_song->setLoop(true, part->end);
    }
    else
    {
        m_song->setPlayFrom(CBarPosition());
        m_song->setLoop(false, m_song->getPlayUpto());
    }
    if (m_topBar)
        m_topBar->updateSectionSummary();
    updatePartButtons();
}

// Checks that there is a start and an end to save as a part
bool GuiSidePanel::hasSectionForPart()
{
    if (m_song && m_song->isLoopActive())
        return true;
    QMessageBox::information(this, tr("Save a Section"),
        tr("First set the start and the end of the section in \"Start / Repeat\" (the button with the arrow "
           "in the top bar), with \"Repeat up to the end bar\" ticked."));
    return false;
}

void GuiSidePanel::addPart()
{
    if (!m_library || !hasSectionForPart())
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New Section"), tr("Name of the section:"), QLineEdit::Normal,
                                               tr("Section %1").arg(m_parts.size() + 1), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    const qint64 id = m_library->addPart(m_songFileName, name, m_song->getPlayFrom(), m_song->getPlayUpto());
    loadParts(id);
}

void GuiSidePanel::updatePart()
{
    const CSongPart *selected = selectedPart();
    if (!m_library || !selected || !hasSectionForPart())
        return;
    if (QMessageBox::question(this, tr("Update Section"),
            tr("Replace the start and end of \"%1\" with the current ones?").arg(selected->name)) != QMessageBox::Yes)
        return;
    CSongPart part = *selected;
    part.start = m_song->getPlayFrom();
    part.end = m_song->getPlayUpto();
    m_library->updatePart(part);
    loadParts(part.id);
}

void GuiSidePanel::renamePart()
{
    const CSongPart *selected = selectedPart();
    if (!m_library || !selected)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename Section"), tr("Name of the section:"), QLineEdit::Normal,
                                               selected->name, &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    CSongPart part = *selected;
    part.name = name;
    m_library->updatePart(part);
    loadParts(part.id);
}

void GuiSidePanel::deletePart()
{
    const CSongPart *selected = selectedPart();
    if (!m_library || !selected)
        return;
    if (QMessageBox::question(this, tr("Delete Section"),
            tr("Delete the section \"%1\"?").arg(selected->name)) != QMessageBox::Yes)
        return;
    m_library->deletePart(selected->id);
    loadParts();
    partSelected(0); // back to the whole song
}

void GuiSidePanel::init(CSong* songObj, CTrackList* trackList, GuiTopBar* topBar)
{
    m_song = songObj;
    m_trackList = trackList;
    m_topBar = topBar;
    m_trackList->init(songObj, m_settings);

    // set skill from config
    playMode_t skill = m_settings->value("SidePanel/skill",PB_PLAY_MODE_followYou).toInt();
    switch (skill) {
        case PB_PLAY_MODE_listen:
            listenRadio->setChecked(true);
            on_listenRadio_toggled(true);
            break;
        case PB_PLAY_MODE_rhythmTapping:
            rhythmTapRadio->setChecked(true);
            on_rhythmTapRadio_toggled(true);
            break;
        case PB_PLAY_MODE_followYou:
            followYouRadio->setChecked(true);
            on_followYouRadio_toggled(true);
            break;
        case PB_PLAY_MODE_playAlong:
            playAlongRadio->setChecked(true);
            on_playAlongRadio_toggled(true);
            break;
        default:
            break;
    }

    bothHandsRadio->setChecked(true);

    rhythmTappingCombo->addItem(tr("Drums"));
    rhythmTappingCombo->addItem(tr("Melody"));
    //FIXME rhythmTappingCombo->addItem(tr("Drums+M"));

    on_rhythmTappingCombo_activated(m_settings->value("SidePanel/rhythmTapping",0).toInt());
    rhythmTappingCombo->setCurrentIndex(m_song->cfg_rhythmTapping);

    repeatSong->setChecked(m_settings->value("SidePanel/repeatSong",false).toBool());
    connect(repeatSong,SIGNAL(stateChanged(int)),this,SLOT(on_repeatSong_released()));

    boostSlider->setMinimum(-100);
    boostSlider->setMaximum(100);
    pianoSlider->setMinimum(-100);
    pianoSlider->setMaximum(100);

    QAction* act;
    act = new QAction(tr("Set as Right Hand Part"), this);
    trackListWidget->addAction(act);
    connect(act, SIGNAL(triggered()), this, SLOT(setTrackRightHandPart()));

    act = new QAction(tr("Set as Left Hand Part"), this);
    trackListWidget->addAction(act);
    connect(act, SIGNAL(triggered()), this, SLOT(setTrackLeftHandPart()));

    act = new QAction(tr("Reset Both Parts"), this);
    trackListWidget->addAction(act);
    connect(act, SIGNAL(triggered()), this, SLOT(clearTrackPart()));

    trackListWidget->setContextMenuPolicy(Qt::ActionsContextMenu);
}

void GuiSidePanel::refresh() {
    if (m_trackList)
    {
        m_trackList->refresh();
        trackListWidget->clear();
        trackListWidget->addItems(m_trackList->getAllChannelProgramNames());
        trackListWidget->setCurrentRow(m_trackList->getActiveItemIndex());
        for (int i = 0; i < trackListWidget->count(); i++)
            m_trackList->changeListWidgetItemView(i, trackListWidget->item(i));
    }
    autoSetMuteYourPart();
}

void GuiSidePanel::loadBookList()
{
    QStringList bookNames = m_settings->getBookList();

    bookCombo->clear();

    for (int i = 0; i < bookNames.size(); i++)
    {
        bookCombo->addItem( bookNames.at(i));
        if (bookNames.at(i) == m_settings->getCurrentBookName())
            bookCombo->setCurrentIndex(i);
    }
    on_bookCombo_activated(-1);
}

void GuiSidePanel::on_bookCombo_activated (int index)
{
    QString currentSong;

    m_settings->setCurrentBookName(bookCombo->currentText(), (index >= 0)? true : false);

    currentSong = m_settings->getCurrentSongName();

    songCombo->clear();
    QStringList songNames = m_settings->getSongList();

    for (int i = 0; i < songNames.size(); ++i)
    {
        songCombo->addItem( songNames.at(i));
        if (songNames.at(i) == currentSong)
            songCombo->setCurrentIndex(i);
    }
    on_songCombo_activated(0); // Now load the selected song
}

void GuiSidePanel::on_songCombo_activated(int index)
{
    Q_UNUSED(index)
    m_settings->setCurrentSongName(songCombo->currentText());
}

void GuiSidePanel::on_rightHandRadio_toggled (bool checked)
{
    if (checked)
        m_settings->setActiveHand(PB_PART_right);
}

void GuiSidePanel::on_bothHandsRadio_toggled (bool checked)
{
    if (checked)
        m_settings->setActiveHand(PB_PART_both);
}

void GuiSidePanel::on_leftHandRadio_toggled (bool checked)
{
    if (checked)
        m_settings->setActiveHand(PB_PART_left);
}

void GuiSidePanel::on_repeatSong_released(){
    m_settings->setValue("SidePanel/repeatSong",repeatSong->isChecked());
}

void GuiSidePanel::autoSetMuteYourPart()
{
    bool checked = true;
    if (m_song->getPlayMode() == PB_PLAY_MODE_rhythmTapping)
    {
        if (m_song->cfg_rhythmTapping == PB_RHYTHM_TAP_drumsOnly)
            checked = false;
    }

    muteYourPartCheck->setChecked(checked);
    m_song->mutePianistPart(checked);
}

void GuiSidePanel::setSongName(const QString &songName)
{
    for (int i = 0; i < songCombo->count(); ++i)
    {
        if (songCombo->itemText(i) == songName)
            songCombo->setCurrentIndex(i);
    }
}

void GuiSidePanel::setBookName(const QString &bookName)
{
    for (int i = 0; i < bookCombo->count(); ++i)
    {
        if (bookCombo->itemText(i) == bookName)
            bookCombo->setCurrentIndex(i);
    }
}

// pass either 'left' 'right' or 'both'
void GuiSidePanel::setCurrentHand(const QString &hand)
{
    if (hand == "left")
        leftHandRadio->setChecked(true);
    else if (hand == "right")
        rightHandRadio->setChecked(true);
    else
        bothHandsRadio->setChecked(true);
}

void GuiSidePanel::updateTranslate(){
    // save original
    if (listWidgetsRetranslateUi.size()==0){
        QList<QWidget*> l2 = this->findChildren<QWidget *>();
        for (auto &w:l2){
            QMap<QString,QString> m;
            m["toolTip"]=w->toolTip();
            m["whatsThis"]=w->whatsThis();
            m["windowTitle"]=w->windowTitle();
            m["statusTip"]=w->statusTip();
            listWidgetsRetranslateUi[w]=m;
        }

        QList<QAction*> l = this->findChildren<QAction *>();
        for (auto &w:l){
            QMap<QString,QString> m;
            m["toolTip"]=w->toolTip();
            m["whatsThis"]=w->whatsThis();
            m["statusTip"]=w->statusTip();
            m["text"]=w->text();
            listActionsRetranslateUi[w]=m;
        }

    }

    // retranslate UI
    QList<QWidget*> l2 = this->findChildren<QWidget *>();
    for (auto &w:l2){
        if (!w->toolTip().isEmpty()) w->setToolTip(tr(listWidgetsRetranslateUi[w]["toolTip"].toStdString().c_str()));
        if (!w->whatsThis().isEmpty()) w->setWhatsThis(tr(listWidgetsRetranslateUi[w]["whatsThis"].toStdString().c_str()));
        if (!w->windowTitle().isEmpty()) w->setWindowTitle(tr(listWidgetsRetranslateUi[w]["windowTitle"].toStdString().c_str()));
        if (!w->statusTip().isEmpty()) w->setStatusTip(tr(listWidgetsRetranslateUi[w]["statusTip"].toStdString().c_str()));
    }

    QList<QAction*> l = this->findChildren<QAction *>();
    for (auto &w:l){
        if (!w->toolTip().isEmpty()) w->setToolTip(tr(listActionsRetranslateUi[w]["toolTip"].toStdString().c_str()));
        if (!w->whatsThis().isEmpty()) w->setWhatsThis(tr(listActionsRetranslateUi[w]["whatsThis"].toStdString().c_str()));
        if (!w->statusTip().isEmpty()) w->setStatusTip(tr(listActionsRetranslateUi[w]["statusTip"].toStdString().c_str()));
        if (!w->text().isEmpty()) w->setText(tr(listActionsRetranslateUi[w]["text"].toStdString().c_str()));
    }

    rhythmTappingCombo->setItemText(0,tr("Drums"));
    rhythmTappingCombo->setItemText(1,tr("Melody"));

    retranslateUi(this);

    // the song header, which is not in the .ui file
    setPartTexts();
    m_libraryButton->setText(tr("Library..."));
    m_libraryButton->setToolTip(tr("Choose a song from the library: favourites, recent songs and your folders"));
    setSongInfo(m_songTitle, m_songFileName, m_songInfo); // its tooltips were reset above

    // ---  smart resize panel  --- //
    int maxDeltaWidth=0;
    this->setMaximumWidth(300); // default
    QVector<QWidget*> listCheckWidget {label2,listenRadio,rhythmTapRadio,followYouRadio,playAlongRadio,rightHandRadio,bothHandsRadio,leftHandRadio};

    for (QWidget* w:listCheckWidget){
        int delta = 0;
        QFontMetrics fm(w->font());

#if (QT_VERSION < QT_VERSION_CHECK(5, 11, 0)) // keep compat with Qt < 5.11
#define horizontalAdvance width
#endif
        auto *const lb = qobject_cast<QLabel*>(w);
        if (lb) delta=fm.horizontalAdvance(lb->text())-lb->width();

        auto *const rb = qobject_cast<QRadioButton*>(w);
        if (rb) delta=fm.horizontalAdvance(rb->text())-(rb->width()-32);

        if (delta>maxDeltaWidth) maxDeltaWidth=delta;
    }
    this->setMaximumWidth(300+maxDeltaWidth);
}

void GuiSidePanel::on_rhythmTappingCombo_activated (int index)
{
    m_settings->setValue("SidePanel/rhythmTapping",index);

    switch (index)
    {
        case 1 :
            m_song->cfg_rhythmTapping = PB_RHYTHM_TAP_mellodyOnly;
            break;
        case 2 :
            m_song->cfg_rhythmTapping = PB_RHYTHM_TAP_drumsAndMellody;
           break;
        default:
            m_song->cfg_rhythmTapping = PB_RHYTHM_TAP_drumsOnly;
    }
    autoSetMuteYourPart();
}

