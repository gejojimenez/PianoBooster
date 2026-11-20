/*********************************************************************************/
/*!
@file           GuiSectionEditor.cpp

@brief          The section editor: creates and changes the named sections of a song
                on a piano roll of the whole song.

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

#include "GuiSectionEditor.h"
#include "GuiPianoRoll.h"
#include "SectionPlayer.h"
#include "MidiDevice.h"

#include <QtWidgets>

#include <cmath>

namespace {

// A beat counting from 1, with two decimals, as it is shown in the fields
double shownBeat(double beatFromZero)
{
    return std::round((beatFromZero + 1.0) * 100.0) / 100.0;
}

QString durationText(double ms)
{
    const qint64 seconds = static_cast<qint64>(std::llround(ms / 1000.0));
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
}

QPushButton *makeButton(const QString &text, const QString &toolTip)
{
    auto *button = new QPushButton(text);
    button->setToolTip(toolTip);
    button->setAutoDefault(false); // Enter in the fields must not press a button
    return button;
}

} // namespace

GuiSectionEditor::GuiSectionEditor(CLibrary *library, const QString &songPath, const QString &songTitle,
                                   CMidiDevice *device, double speed,
                                   const QStringList &instrumentNames, const QVector<int> &instrumentChannels, int activeChannel,
                                   QWidget *parent)
    : QDialog(parent), m_library(library), m_songPath(songPath), m_device(device)
{
    setWindowTitle(tr("Section Editor - %1").arg(songTitle));
    setWindowFlags(windowFlags() | Qt::WindowMaximizeButtonHint | Qt::WindowMinimizeButtonHint);
    setSizeGripEnabled(true);
    m_map.load(songPath);

    m_player = new CSectionPlayer(&m_map, device, this);

    // the views
    m_overview = new CSongOverview;
    m_roll = new CPianoRollView;
    m_overview->setSongMap(&m_map);
    m_roll->setSongMap(&m_map);

    // the tools above the views
    auto *zoomOutButton = makeButton(QStringLiteral("−"), tr("Zoom out (Ctrl + mouse wheel)"));
    auto *zoomInButton = makeButton(QStringLiteral("+"), tr("Zoom in (Ctrl + mouse wheel)"));
    auto *fitSongButton = makeButton(tr("Whole Song"), tr("Show the whole song"));
    auto *fitSelectionButton = makeButton(tr("Selection"), tr("Show the whole selection"));
    for (QPushButton *button : { zoomOutButton, zoomInButton })
        button->setFixedWidth(button->fontMetrics().horizontalAdvance(QStringLiteral("MM")) + 12);

    m_snapCombo = new QComboBox;
    m_snapCombo->addItem(tr("Bar"), static_cast<int>(CSnap::Bar));
    m_snapCombo->addItem(tr("Beat"), static_cast<int>(CSnap::Beat));
    m_snapCombo->addItem(tr("Half beat"), static_cast<int>(CSnap::HalfBeat));
    m_snapCombo->addItem(tr("Quarter beat"), static_cast<int>(CSnap::QuarterBeat));
    m_snapCombo->addItem(tr("Free"), static_cast<int>(CSnap::None));
    m_snapCombo->setCurrentIndex(1);
    m_snapCombo->setToolTip(tr("What the start and the end of the selection go to when they are dragged"));

    // the instrument practised, as in the list of the practice window
    m_instrumentCombo = new QComboBox;
    m_instrumentCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    for (int i = 0; i < instrumentNames.size() && i < instrumentChannels.size(); ++i)
    {
        m_instrumentCombo->addItem(instrumentNames[i], instrumentChannels[i]);
        if (instrumentChannels[i] == activeChannel)
            m_instrumentCombo->setCurrentIndex(i);
    }
    m_instrumentCombo->setEnabled(m_instrumentCombo->count() > 1);
    m_instrumentCombo->setToolTip(tr("The instrument (MIDI channel) to practise: its notes are shown in colour, by hand. "
                                     "\"Practise\" also chooses it in the practice window."));
    m_roll->setPractiseChannel(practiseChannel());
    m_overview->setPractiseChannel(practiseChannel());

    m_otherPartsCheck = new QCheckBox(tr("Other instruments"));
    m_otherPartsCheck->setChecked(true);
    m_otherPartsCheck->setToolTip(tr("Show the notes of the other instruments too, in grey (the drums are not shown)"));

    auto *legend = new QLabel(QStringLiteral("<span style='color:#2870d8'>■</span> %1 &nbsp; "
                                             "<span style='color:#239650'>■</span> %2 &nbsp; "
                                             "<span style='color:#a5a5a5'>■</span> %3")
                              .arg(tr("Right hand"), tr("Left hand"), tr("Others")));

    auto *tools = new QHBoxLayout;
    tools->addWidget(new QLabel(tr("Zoom:")));
    tools->addWidget(zoomOutButton);
    tools->addWidget(zoomInButton);
    tools->addWidget(fitSongButton);
    tools->addWidget(fitSelectionButton);
    tools->addSpacing(16);
    tools->addWidget(new QLabel(tr("Snap to:")));
    tools->addWidget(m_snapCombo);
    tools->addSpacing(16);
    tools->addWidget(new QLabel(tr("Instrument:")));
    tools->addWidget(m_instrumentCombo);
    tools->addWidget(m_otherPartsCheck);
    tools->addStretch(1);
    tools->addWidget(legend);

    auto *hint = new QLabel(tr("Drag on the ruler or on the notes to select a stretch of the song, and drag its edges to change it. "
                               "The mouse wheel scrolls; click a section on the strip under the ruler to choose it."));
    hint->setWordWrap(true);
    QFont hintFont = hint->font();
    hintFont.setPointSizeF(hintFont.pointSizeF() * 0.9);
    hint->setFont(hintFont);
    hint->setForegroundRole(QPalette::PlaceholderText);

    // the saved sections
    m_partList = new QListWidget;
    m_partList->setMinimumWidth(220);
    m_partList->setMaximumHeight(140);
    m_newButton = makeButton(tr("New..."), tr("Save the selection as a new section"));
    m_saveButton = makeButton(tr("Save"), tr("Save the changes to the start and end of the chosen section"));
    m_renameButton = makeButton(tr("Rename..."), tr("Rename the chosen section"));
    m_deleteButton = makeButton(tr("Delete"), tr("Delete the chosen section"));
    auto *partButtons = new QVBoxLayout;
    for (QPushButton *button : { m_newButton, m_saveButton, m_renameButton, m_deleteButton })
        partButtons->addWidget(button);
    partButtons->addStretch(1);
    auto *partsBox = new QGroupBox(tr("Sections"));
    auto *partsLayout = new QHBoxLayout(partsBox);
    partsLayout->addWidget(m_partList, 1);
    partsLayout->addLayout(partButtons);

    // the selection, as bars and beats
    m_startBarSpin = new QSpinBox;
    m_startBeatSpin = new QDoubleSpinBox;
    m_endBarSpin = new QSpinBox;
    m_endBeatSpin = new QDoubleSpinBox;
    const int bars = m_map.barCount();
    m_startBarSpin->setRange(FIRST_BAR_NUMBER, bars - 1 + FIRST_BAR_NUMBER);
    m_endBarSpin->setRange(FIRST_BAR_NUMBER, bars + FIRST_BAR_NUMBER); // up to the end of the last bar
    for (QDoubleSpinBox *spin : { m_startBeatSpin, m_endBeatSpin })
    {
        spin->setDecimals(2);
        spin->setRange(1.0, 4.99);
        spin->setSingleStep(1.0);
    }
    m_startBarSpin->setToolTip(tr("The bar where the selection starts"));
    m_startBeatSpin->setToolTip(tr("The beat where the selection starts (1 is the start of the bar)"));
    m_endBarSpin->setToolTip(tr("The selection ends just before this bar and beat: they are not included"));
    m_endBeatSpin->setToolTip(m_endBarSpin->toolTip());
    m_infoLabel = new QLabel;
    m_infoLabel->setWordWrap(true);
    m_infoLabel->setMinimumWidth(260);
    auto *selectionBox = new QGroupBox(tr("Selection"));
    auto *selectionLayout = new QGridLayout(selectionBox);
    selectionLayout->addWidget(new QLabel(tr("Starts at:")), 0, 0);
    selectionLayout->addWidget(new QLabel(tr("bar")), 0, 1);
    selectionLayout->addWidget(m_startBarSpin, 0, 2);
    selectionLayout->addWidget(new QLabel(tr("beat")), 0, 3);
    selectionLayout->addWidget(m_startBeatSpin, 0, 4);
    selectionLayout->addWidget(new QLabel(tr("Ends before:")), 1, 0);
    selectionLayout->addWidget(new QLabel(tr("bar")), 1, 1);
    selectionLayout->addWidget(m_endBarSpin, 1, 2);
    selectionLayout->addWidget(new QLabel(tr("beat")), 1, 3);
    selectionLayout->addWidget(m_endBeatSpin, 1, 4);
    selectionLayout->addWidget(m_infoLabel, 2, 0, 1, 5);
    selectionLayout->setRowStretch(3, 1);
    selectionLayout->setColumnStretch(5, 1); // keep the fields together on the left

    // listening
    m_playButton = makeButton(tr("Play"), tr("Listen to the selection, or to the song from the place shown if nothing is selected (Space)"));
    m_playButton->setIcon(QIcon(":/images/play.png"));
    m_repeatCheck = new QCheckBox(tr("Repeat"));
    m_repeatCheck->setToolTip(tr("Start the selection again when it ends"));
    m_speedSpin = new QSpinBox;
    m_speedSpin->setRange(20, 200);
    m_speedSpin->setSingleStep(5);
    m_speedSpin->setSuffix(QStringLiteral(" %"));
    m_speedSpin->setValue(qBound(20, static_cast<int>(std::lround(speed * 100.0)), 200));
    auto *listenBox = new QGroupBox(tr("Listen"));
    auto *listenLayout = new QGridLayout(listenBox);
    listenLayout->addWidget(m_playButton, 0, 0, 1, 2);
    listenLayout->addWidget(m_repeatCheck, 1, 0, 1, 2);
    listenLayout->addWidget(new QLabel(tr("Speed:")), 2, 0);
    listenLayout->addWidget(m_speedSpin, 2, 1);
    listenLayout->setRowStretch(3, 1);

    m_practiseButton = makeButton(tr("Practise"), tr("Close the editor and practise the selection, repeating it"));
    auto *closeButton = makeButton(tr("Close"), QString());
    auto *endButtons = new QVBoxLayout;
    endButtons->addWidget(m_practiseButton);
    endButtons->addWidget(closeButton);
    endButtons->addStretch(1);

    auto *bottom = new QHBoxLayout;
    bottom->addWidget(partsBox, 2);
    bottom->addWidget(selectionBox, 2);
    bottom->addWidget(listenBox);
    bottom->addLayout(endButtons);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(tools);
    layout->addWidget(m_overview);
    layout->addWidget(m_roll, 1);
    layout->addWidget(hint);
    layout->addLayout(bottom);

    // connections
    connect(zoomOutButton, &QPushButton::clicked, this, [this]() { m_roll->zoomBy(1.0 / 1.5); });
    connect(zoomInButton, &QPushButton::clicked, this, [this]() { m_roll->zoomBy(1.5); });
    connect(fitSongButton, &QPushButton::clicked, m_roll, &CPianoRollView::zoomToSong);
    connect(fitSelectionButton, &QPushButton::clicked, this, [this]() {
        if (m_roll->hasSelection())
            m_roll->zoomToFit(m_selStart, m_selEnd);
    });
    connect(m_snapCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        m_roll->setSnap(static_cast<CSnap>(m_snapCombo->currentData().toInt()));
    });
    connect(m_instrumentCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        m_roll->setPractiseChannel(practiseChannel());
        m_overview->setPractiseChannel(practiseChannel());
    });
    connect(m_otherPartsCheck, &QCheckBox::toggled, this, [this](bool show) {
        m_roll->setShowOtherParts(show);
        m_overview->setShowOtherParts(show);
    });
    connect(m_roll, &CPianoRollView::selectionChanged, this, &GuiSectionEditor::rollSelectionChanged);
    connect(m_roll, &CPianoRollView::newSelectionStarted, this, &GuiSectionEditor::rollNewSelection);
    connect(m_roll, &CPianoRollView::partClicked, this, &GuiSectionEditor::partClicked);
    connect(m_roll, &CPianoRollView::viewChanged, this, &GuiSectionEditor::updateOverviewRange);
    connect(m_overview, &CSongOverview::centreRequested, m_roll, &CPianoRollView::centreOn);
    connect(m_partList, &QListWidget::currentRowChanged, this, &GuiSectionEditor::partRowChanged);
    connect(m_newButton, &QPushButton::clicked, this, &GuiSectionEditor::newPart);
    connect(m_saveButton, &QPushButton::clicked, this, &GuiSectionEditor::savePart);
    connect(m_renameButton, &QPushButton::clicked, this, &GuiSectionEditor::renamePart);
    connect(m_deleteButton, &QPushButton::clicked, this, &GuiSectionEditor::deletePart);
    connect(m_startBarSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &GuiSectionEditor::startEdited);
    connect(m_startBeatSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &GuiSectionEditor::startEdited);
    connect(m_endBarSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &GuiSectionEditor::endEdited);
    connect(m_endBeatSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &GuiSectionEditor::endEdited);
    connect(m_playButton, &QPushButton::clicked, this, &GuiSectionEditor::togglePlay);
    connect(m_repeatCheck, &QCheckBox::toggled, m_player, &CSectionPlayer::setRepeat);
    connect(m_speedSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        m_player->setSpeed(value / 100.0);
    });
    connect(m_player, &CSectionPlayer::positionChanged, this, &GuiSectionEditor::playerPosition);
    connect(m_player, &CSectionPlayer::stopped, this, &GuiSectionEditor::playerStopped);
    connect(m_practiseButton, &QPushButton::clicked, this, &GuiSectionEditor::practise);
    connect(closeButton, &QPushButton::clicked, this, &GuiSectionEditor::reject);
    auto *playShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(playShortcut, &QShortcut::activated, this, &GuiSectionEditor::togglePlay);

    if (parent)
        resize(parent->size() * 0.85);
    else
        resize(1100, 700);

    loadParts(-1);
    setSelection(0, 0);
    showSelectionInFields();
}

GuiSectionEditor::~GuiSectionEditor()
{
    // stop the sound before the song is gone
    m_player->blockSignals(true);
    m_player->stop();
}

int GuiSectionEditor::practiseChannel() const
{
    return m_instrumentCombo->count() > 0 ? m_instrumentCombo->currentData().toInt() : -1;
}

void GuiSectionEditor::setInitialSelection(qint64 partId, bool loopActive, const CBarPosition &loopStart, const CBarPosition &loopEnd)
{
    for (int row = 0; row < m_parts.size(); ++row)
    {
        if (m_parts[row].id == partId)
        {
            m_partList->setCurrentRow(row); // shows its start and end
            return;
        }
    }
    if (loopActive)
    {
        setSelection(m_map.tickOfPosition(loopStart), m_map.sectionEndTick(loopEnd));
        showSelectionInFields();
    }
}

void GuiSectionEditor::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    if (!m_firstShow)
        return;
    m_firstShow = false;
    // the size of the piano roll is known only once it is shown
    QTimer::singleShot(0, this, [this]() {
        if (m_roll->hasSelection())
            m_roll->ensureVisible(m_selStart, m_selEnd);
        updateOverviewRange();
    });
}

void GuiSectionEditor::loadParts(qint64 selectId)
{
    m_parts = m_library ? m_library->parts(m_songPath) : QVector<CSongPart>();
    m_currentPartId = -1;
    {
        const QSignalBlocker block(m_partList);
        m_partList->clear();
        for (const CSongPart &part : m_parts)
        {
            auto *item = new QListWidgetItem(part.name + QStringLiteral("   (") + partRangeText(part) + QStringLiteral(")"));
            item->setData(Qt::UserRole, part.id);
            m_partList->addItem(item);
            if (part.id == selectId)
            {
                m_partList->setCurrentItem(item);
                m_currentPartId = part.id;
            }
        }
        if (m_currentPartId < 0)
            m_partList->setCurrentRow(-1);
    }
    m_roll->setParts(m_parts, m_currentPartId);
    m_overview->setParts(m_parts, m_currentPartId);
    updateInfo();
    updateButtons();
}

const CSongPart *GuiSectionEditor::currentPart() const
{
    for (const CSongPart &part : m_parts)
        if (part.id == m_currentPartId)
            return &part;
    return nullptr;
}

bool GuiSectionEditor::isDirty() const
{
    const CSongPart *part = currentPart();
    if (!part || m_selEnd <= m_selStart)
        return false;
    return m_map.tickOfPosition(part->start) != m_selStart || m_map.sectionEndTick(part->end) != m_selEnd;
}

bool GuiSectionEditor::resolveChanges()
{
    const CSongPart *part = currentPart();
    if (!part || !isDirty())
        return true;
    const auto answer = QMessageBox::question(this, tr("Section Changed"),
        tr("The start or the end of \"%1\" has been changed. Save the changes?").arg(part->name),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Save)
        savePart();
    return true;
}

void GuiSectionEditor::partRowChanged(int row)
{
    const qint64 id = (row >= 0 && m_partList->item(row)) ? m_partList->item(row)->data(Qt::UserRole).toLongLong() : -1;
    if (id == m_currentPartId)
        return;
    if (!resolveChanges())
    {
        // stay on the section that has been changed
        const QSignalBlocker block(m_partList);
        for (int i = 0; i < m_partList->count(); ++i)
            if (m_partList->item(i)->data(Qt::UserRole).toLongLong() == m_currentPartId)
                m_partList->setCurrentRow(i);
        return;
    }
    m_currentPartId = id;
    m_roll->setParts(m_parts, m_currentPartId);
    m_overview->setParts(m_parts, m_currentPartId);
    if (const CSongPart *part = currentPart())
    {
        setSelection(m_map.tickOfPosition(part->start), m_map.sectionEndTick(part->end));
        showSelectionInFields();
        m_roll->ensureVisible(m_selStart, m_selEnd);
    }
    updateInfo();
    updateButtons();
}

void GuiSectionEditor::partClicked(qint64 id)
{
    for (int row = 0; row < m_partList->count(); ++row)
        if (m_partList->item(row)->data(Qt::UserRole).toLongLong() == id)
            m_partList->setCurrentRow(row);
}

// A new selection is being made away from the edges: it is no longer the chosen
// section, unless that section has been changed and not saved yet.
void GuiSectionEditor::rollNewSelection()
{
    if (m_currentPartId < 0 || isDirty())
        return;
    m_currentPartId = -1;
    {
        const QSignalBlocker block(m_partList);
        m_partList->setCurrentRow(-1);
    }
    m_roll->setParts(m_parts, -1);
    m_overview->setParts(m_parts, -1);
}

void GuiSectionEditor::rollSelectionChanged(qint64 start, qint64 end)
{
    setSelection(start, end);
    showSelectionInFields();
}

void GuiSectionEditor::setSelection(qint64 start, qint64 end)
{
    m_selStart = start;
    m_selEnd = end;
    if (m_roll->selectionStart() != start || m_roll->selectionEnd() != end)
        m_roll->setSelection(start, end);
    m_overview->setSelection(start, end);
    updateInfo();
    updateButtons();
}

void GuiSectionEditor::showSelectionInFields()
{
    const QSignalBlocker b1(m_startBarSpin);
    const QSignalBlocker b2(m_startBeatSpin);
    const QSignalBlocker b3(m_endBarSpin);
    const QSignalBlocker b4(m_endBeatSpin);

    const int startBar = m_map.barOfTick(m_selStart);
    m_startBarSpin->setValue(startBar + FIRST_BAR_NUMBER);
    m_startBeatSpin->setMaximum(m_map.beatsInBar(startBar) + 0.99);
    m_startBeatSpin->setValue(shownBeat(m_map.beatOfTick(m_selStart)));

    const int endBar = m_map.barOfTick(m_selEnd);
    m_endBarSpin->setValue(endBar + FIRST_BAR_NUMBER);
    m_endBeatSpin->setMaximum(m_map.beatsInBar(endBar) + 0.99);
    m_endBeatSpin->setValue(shownBeat(m_map.beatOfTick(m_selEnd)));
}

void GuiSectionEditor::startEdited()
{
    const int bar = m_startBarSpin->value() - FIRST_BAR_NUMBER;
    {
        const QSignalBlocker block(m_startBeatSpin);
        m_startBeatSpin->setMaximum(m_map.beatsInBar(bar) + 0.99);
    }
    setSelection(m_map.tickOfBarBeat(bar, m_startBeatSpin->value() - 1.0), m_selEnd);
}

void GuiSectionEditor::endEdited()
{
    const int bar = m_endBarSpin->value() - FIRST_BAR_NUMBER;
    {
        const QSignalBlocker block(m_endBeatSpin);
        m_endBeatSpin->setMaximum(m_map.beatsInBar(bar) + 0.99);
    }
    setSelection(m_selStart, std::min(m_map.tickOfBarBeat(bar, m_endBeatSpin->value() - 1.0), m_map.endTick()));
}

QString GuiSectionEditor::rangeText(qint64 start, qint64 end) const
{
    const int firstBar = m_map.barOfTick(start) + FIRST_BAR_NUMBER;
    const int lastBar = m_map.barOfTick(end - 1) + FIRST_BAR_NUMBER;
    return (firstBar == lastBar) ? tr("bar %1").arg(firstBar) : tr("bars %1-%2").arg(firstBar).arg(lastBar);
}

QString GuiSectionEditor::partRangeText(const CSongPart &part) const
{
    return rangeText(m_map.tickOfPosition(part.start), m_map.sectionEndTick(part.end));
}

void GuiSectionEditor::updateInfo()
{
    if (m_selEnd <= m_selStart)
    {
        m_infoLabel->setText(tr("Nothing is selected: drag on the ruler or on the notes."));
        return;
    }
    const int bars = m_map.barOfTick(m_selEnd - 1) - m_map.barOfTick(m_selStart) + 1;
    QString text = rangeText(m_selStart, m_selEnd) + QString::fromUtf8(" · ") + tr("%n bar(s)", nullptr, bars) +
                   QString::fromUtf8(" · ") + durationText(m_map.msOfTick(m_selEnd) - m_map.msOfTick(m_selStart));
    if (isDirty())
        text += QStringLiteral("\n") + tr("Changed: press \"Save\" to keep it in \"%1\", or \"New\" to make another section.")
                                         .arg(currentPart()->name);
    else if (const CSongPart *part = currentPart())
        text += QStringLiteral("\n") + tr("Section \"%1\"").arg(part->name);
    m_infoLabel->setText(text);
}

void GuiSectionEditor::updateButtons()
{
    const bool selection = m_selEnd > m_selStart;
    const bool part = currentPart() != nullptr;
    m_newButton->setEnabled(selection && m_library);
    m_saveButton->setEnabled(isDirty());
    m_renameButton->setEnabled(part);
    m_deleteButton->setEnabled(part);
    m_practiseButton->setEnabled(selection);
}

void GuiSectionEditor::newPart()
{
    if (!m_library || m_selEnd <= m_selStart)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New Section"), tr("Name of the section:"), QLineEdit::Normal,
                                               tr("Section %1").arg(m_parts.size() + 1), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    // a changed section keeps what was saved: the changes go to the new section
    const qint64 id = m_library->addPart(m_songPath, name, m_map.positionOfTick(m_selStart), m_map.sectionEndPosition(m_selEnd));
    loadParts(id);
}

void GuiSectionEditor::savePart()
{
    const CSongPart *current = currentPart();
    if (!m_library || !current || m_selEnd <= m_selStart)
        return;
    CSongPart part = *current;
    part.start = m_map.positionOfTick(m_selStart);
    part.end = m_map.sectionEndPosition(m_selEnd);
    m_library->updatePart(part);
    loadParts(part.id);
}

void GuiSectionEditor::renamePart()
{
    const CSongPart *current = currentPart();
    if (!m_library || !current)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename Section"), tr("Name of the section:"), QLineEdit::Normal,
                                               current->name, &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    // only the name: changes to the start and end are still to be saved
    CSongPart part = *current;
    part.name = name;
    m_library->updatePart(part);
    loadParts(part.id);
}

void GuiSectionEditor::deletePart()
{
    const CSongPart *current = currentPart();
    if (!m_library || !current)
        return;
    if (QMessageBox::question(this, tr("Delete Section"), tr("Delete the section \"%1\"?").arg(current->name)) != QMessageBox::Yes)
        return;
    m_library->deletePart(current->id);
    loadParts(-1); // the selection stays, to make a new section with it if wanted
}

void GuiSectionEditor::togglePlay()
{
    if (m_player->isPlaying())
    {
        m_player->stop();
        return;
    }
    if (!m_device || !m_device->validMidiOutput())
    {
        QMessageBox::information(this, tr("Listen"),
            tr("There is no MIDI output to play through. Choose one in \"Setup\" / \"MIDI Setup\"."));
        return;
    }
    qint64 start = m_selStart;
    qint64 end = m_selEnd;
    if (end <= start)
    {
        // nothing selected: from the place shown up to the end of the song
        start = m_roll->snapTick(m_roll->firstVisibleTick());
        end = m_map.endTick();
    }
    m_player->play(start, end, m_repeatCheck->isChecked(), m_speedSpin->value() / 100.0);
    if (m_player->isPlaying())
    {
        m_playButton->setText(tr("Stop"));
        m_playButton->setIcon(QIcon(":/images/stop.png"));
        playerPosition(start);
    }
}

void GuiSectionEditor::playerPosition(qint64 tick)
{
    m_roll->setCursorTick(tick, true);
    m_overview->setCursorTick(tick);
}

void GuiSectionEditor::playerStopped()
{
    m_playButton->setText(tr("Play"));
    m_playButton->setIcon(QIcon(":/images/play.png"));
    m_roll->setCursorTick(-1, false);
    m_overview->setCursorTick(-1);
}

void GuiSectionEditor::updateOverviewRange()
{
    m_overview->setVisibleRange(m_roll->firstVisibleTick(), m_roll->lastVisibleTick());
}

void GuiSectionEditor::practise()
{
    if (m_selEnd <= m_selStart)
        return;
    m_player->stop();
    qint64 partId = m_currentPartId;
    if (const CSongPart *part = currentPart())
    {
        if (isDirty())
        {
            const auto answer = QMessageBox::question(this, tr("Section Changed"),
                tr("The start or the end of \"%1\" has been changed. Save the changes before practising?").arg(part->name),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
            if (answer == QMessageBox::Cancel)
                return;
            if (answer == QMessageBox::Save)
                savePart();
            else
                partId = -1; // practise what is selected, without saving it
        }
    }
    m_practisePartId = partId;
    m_practiseStart = m_map.positionOfTick(m_selStart);
    m_practiseEnd = m_map.sectionEndPosition(m_selEnd);
    QDialog::accept();
}

void GuiSectionEditor::reject()
{
    m_player->stop();
    if (!resolveChanges())
        return;
    QDialog::reject();
}
