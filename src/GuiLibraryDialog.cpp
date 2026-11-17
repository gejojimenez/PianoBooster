/*********************************************************************************/
/*!
@file           GuiLibraryDialog.cpp

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

#include "GuiLibraryDialog.h"

#include <QtWidgets>

namespace {

const int KIND_ROLE = Qt::UserRole;
const int PATH_ROLE = Qt::UserRole + 1;
const int SORT_ROLE = Qt::UserRole + 2;

const int MAX_SONGS_LISTED = 5000;

enum SongColumn { COL_FAVOURITE, COL_NAME, COL_FOLDER, COL_DURATION, COL_BARS, COL_TIME_SIG, COL_PARTS, COL_LAST_OPENED, COL_TIMES_OPENED, COL_COUNT };

const QString STAR = QString::fromUtf8("★");       // ★
const QString EMPTY_STAR = QString::fromUtf8("☆"); // ☆

// Sorts by the value in SORT_ROLE when there is one (numbers, dates), else by the text
class SongItem : public QTreeWidgetItem
{
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem &other) const override
    {
        const int column = treeWidget() ? treeWidget()->sortColumn() : 0;
        const QVariant a = data(column, SORT_ROLE);
        const QVariant b = other.data(column, SORT_ROLE);
        if (a.isValid() && b.isValid())
            return QVariant::compare(a, b) == QPartialOrdering::Less;
        return text(column).localeAwareCompare(other.text(column)) < 0;
    }
};

bool hasSubfolders(const QString &path)
{
    return !QDir(path).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty();
}

} // namespace

GuiLibraryDialog::GuiLibraryDialog(CLibrary *library, const QString &currentSong, QWidget *parent)
    : QDialog(parent), m_library(library), m_currentSong(CLibrary::normalisePath(currentSong))
{
    setWindowTitle(tr("Library"));
    resize(1100, 620);

    // Left: where the songs come from
    m_sources = new QTreeWidget;
    m_sources->setHeaderHidden(true);
    m_sources->setMinimumWidth(230);
    auto *addButton = new QPushButton(tr("Add Location..."));
    addButton->setToolTip(tr("Add a folder with MIDI files to the library"));
    m_removeLocationButton = new QPushButton(tr("Remove"));
    m_removeLocationButton->setToolTip(tr("Remove the selected location from the library (the files are not deleted)"));
    auto *locationButtons = new QHBoxLayout;
    locationButtons->addWidget(addButton);
    locationButtons->addWidget(m_removeLocationButton);
    auto *leftLayout = new QVBoxLayout;
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(m_sources);
    leftLayout->addLayout(locationButtons);
    auto *left = new QWidget;
    left->setLayout(leftLayout);

    // Right: the songs
    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Search..."));
    m_search->setClearButtonEnabled(true);
    m_songs = new QTreeWidget;
    m_songs->setRootIsDecorated(false);
    m_songs->setUniformRowHeights(true);
    m_songs->setAlternatingRowColors(true);
    m_songs->setSortingEnabled(true);
    m_songs->setColumnCount(COL_COUNT);
    m_songs->setHeaderLabels({ STAR, tr("Name"), tr("Folder"), tr("Duration"), tr("Bars"),
                               tr("Time Sig."), tr("Sections"), tr("Last Opened"), tr("Times") });
    m_songs->headerItem()->setToolTip(COL_FAVOURITE, tr("Favourite (click a star to change it)"));
    m_songs->header()->setStretchLastSection(false);
    m_songs->header()->setSectionResizeMode(COL_NAME, QHeaderView::Stretch);
    m_songs->setColumnWidth(COL_FAVOURITE, 30);
    m_songs->setColumnWidth(COL_FOLDER, 200);
    auto *rightLayout = new QVBoxLayout;
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(m_search);
    rightLayout->addWidget(m_songs);
    auto *right = new QWidget;
    right->setLayout(rightLayout);

    auto *splitter = new QSplitter;
    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setStretchFactor(1, 1);

    auto *buttons = new QDialogButtonBox;
    m_openButton = buttons->addButton(tr("Open"), QDialogButtonBox::AcceptRole);
    buttons->addButton(tr("Close"), QDialogButtonBox::RejectRole);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(splitter);
    layout->addWidget(buttons);

    connect(m_sources, &QTreeWidget::itemSelectionChanged, this, &GuiLibraryDialog::sourceChanged);
    connect(m_sources, &QTreeWidget::itemExpanded, this, &GuiLibraryDialog::sourceExpanded);
    connect(m_songs, &QTreeWidget::itemClicked, this, &GuiLibraryDialog::songClicked);
    connect(m_songs, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int column) {
        if (column != COL_FAVOURITE && item) openSelected(); });
    connect(m_songs, &QTreeWidget::itemSelectionChanged, this, &GuiLibraryDialog::updateButtons);
    connect(m_search, &QLineEdit::textChanged, this, &GuiLibraryDialog::filterSongs);
    connect(addButton, &QPushButton::clicked, this, &GuiLibraryDialog::addLocation);
    connect(m_removeLocationButton, &QPushButton::clicked, this, &GuiLibraryDialog::removeLocation);
    connect(buttons, &QDialogButtonBox::accepted, this, &GuiLibraryDialog::openSelected);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // start in the location of the current song, if it is in one
    QString startLocation;
    for (const QString &location : m_library->locations())
        if (m_currentSong.startsWith(location + '/'))
            startLocation = location;
    fillSources(startLocation);
}

void GuiLibraryDialog::fillSources(const QString &locationToSelect)
{
    m_sources->clear();

    auto *favourites = new QTreeWidgetItem(m_sources, { STAR + " " + tr("Favourites") });
    favourites->setData(0, KIND_ROLE, SOURCE_FAVOURITES);
    auto *recent = new QTreeWidgetItem(m_sources, { QString::fromUtf8("⏲ ") + tr("Recent") });
    recent->setData(0, KIND_ROLE, SOURCE_RECENT);

    auto *locations = new QTreeWidgetItem(m_sources, { tr("Locations") });
    locations->setFlags(Qt::ItemIsEnabled); // a heading, not selectable
    QFont bold = locations->font(0);
    bold.setBold(true);
    locations->setFont(0, bold);

    QTreeWidgetItem *selected = recent;
    for (const QString &path : m_library->locations())
    {
        auto *item = new QTreeWidgetItem(locations, { QDir(path).dirName() });
        item->setData(0, KIND_ROLE, SOURCE_LOCATION);
        item->setData(0, PATH_ROLE, path);
        item->setToolTip(0, QDir::toNativeSeparators(path));
        if (!QDir(path).exists())
        {
            item->setForeground(0, palette().brush(QPalette::Disabled, QPalette::Text));
            item->setToolTip(0, tr("Not found: %1").arg(QDir::toNativeSeparators(path)));
        }
        else if (hasSubfolders(path))
            item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        if (path == locationToSelect)
            selected = item;
    }
    locations->setExpanded(true);
    m_sources->setCurrentItem(selected);
}

void GuiLibraryDialog::addSubfolders(QTreeWidgetItem *item)
{
    if (item->childCount() > 0)
        return;
    const QString path = item->data(0, PATH_ROLE).toString();
    const QStringList folders = QDir(path).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (const QString &name : folders)
    {
        const QString folderPath = path + '/' + name;
        auto *child = new QTreeWidgetItem(item, { name });
        child->setData(0, KIND_ROLE, SOURCE_FOLDER);
        child->setData(0, PATH_ROLE, folderPath);
        child->setToolTip(0, QDir::toNativeSeparators(folderPath));
        if (hasSubfolders(folderPath))
            child->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    }
    if (folders.isEmpty())
        item->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicator);
}

void GuiLibraryDialog::sourceExpanded(QTreeWidgetItem *item)
{
    if (item->data(0, KIND_ROLE).isValid() && item->data(0, PATH_ROLE).isValid())
        addSubfolders(item);
}

void GuiLibraryDialog::sourceChanged()
{
    QTreeWidgetItem *item = m_sources->currentItem();
    updateButtons();
    if (!item || !item->data(0, KIND_ROLE).isValid())
    {
        showSongs({}, QString());
        return;
    }

    switch (item->data(0, KIND_ROLE).toInt())
    {
    case SOURCE_FAVOURITES:
        showSongs(m_library->favourites(), QString());
        break;
    case SOURCE_RECENT:
        showSongs(m_library->recent(50), QString());
        break;
    default:
    {
        // the MIDI files in this folder and its subfolders
        const QString folder = item->data(0, PATH_ROLE).toString();
        QStringList paths;
        QDirIterator it(folder, CLibrary::midiFileFilters(), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext() && paths.size() < MAX_SONGS_LISTED)
            paths << it.next();

        // read the information of the songs that have not been seen before (it is quick),
        // and keep it in the library for the next time
        QVector<CLibrarySong> songs = m_library->songs(paths);
        QApplication::setOverrideCursor(Qt::WaitCursor);
        m_library->beginBatch();
        for (CLibrarySong &song : songs)
            if (!song.info.valid)
            {
                song.info = CMidiInfo::read(song.path);
                m_library->storeInfo(song.path, song.info);
            }
        m_library->endBatch();
        QApplication::restoreOverrideCursor();
        showSongs(songs, folder);
    }
    }
}

void GuiLibraryDialog::showSongs(const QVector<CLibrarySong> &songs, const QString &baseFolder)
{
    m_songs->setSortingEnabled(false);
    m_songs->clear();
    const QLocale locale;
    QTreeWidgetItem *current = nullptr;

    for (const CLibrarySong &song : songs)
    {
        const QFileInfo file(song.path);
        if (!file.exists())
            continue; // deleted or moved since it was opened

        auto *item = new SongItem(m_songs);
        item->setData(0, PATH_ROLE, song.path);
        item->setText(COL_FAVOURITE, song.favourite ? STAR : EMPTY_STAR);
        item->setData(COL_FAVOURITE, SORT_ROLE, song.favourite ? 0 : 1);
        item->setTextAlignment(COL_FAVOURITE, Qt::AlignCenter);
        item->setText(COL_NAME, file.completeBaseName());
        item->setToolTip(COL_NAME, QDir::toNativeSeparators(song.path));

        // the folder, relative to the selected one
        QString folder = file.absolutePath();
        if (!baseFolder.isEmpty())
            folder = QDir(baseFolder).relativeFilePath(folder);
        if (folder == ".")
            folder.clear();
        item->setText(COL_FOLDER, QDir::toNativeSeparators(folder));
        item->setToolTip(COL_FOLDER, QDir::toNativeSeparators(file.absolutePath()));

        if (song.info.valid)
        {
            item->setText(COL_DURATION, song.info.durationText());
            item->setData(COL_DURATION, SORT_ROLE, song.info.durationMs);
            item->setText(COL_BARS, QString::number(song.info.bars));
            item->setData(COL_BARS, SORT_ROLE, song.info.bars);
            item->setText(COL_TIME_SIG, song.info.timeSigText());
        }
        if (song.lastOpened.isValid())
        {
            item->setText(COL_LAST_OPENED, locale.toString(song.lastOpened, QLocale::ShortFormat));
            item->setData(COL_LAST_OPENED, SORT_ROLE, song.lastOpened);
        }
        else
            item->setData(COL_LAST_OPENED, SORT_ROLE, QDateTime::fromSecsSinceEpoch(0));
        item->setText(COL_PARTS, song.partCount > 0 ? QString::number(song.partCount) : QString());
        item->setData(COL_PARTS, SORT_ROLE, song.partCount);
        item->setText(COL_TIMES_OPENED, song.timesOpened > 0 ? QString::number(song.timesOpened) : QString());
        item->setData(COL_TIMES_OPENED, SORT_ROLE, song.timesOpened);
        for (int column : { COL_DURATION, COL_BARS, COL_PARTS, COL_TIMES_OPENED })
            item->setTextAlignment(column, Qt::AlignRight | Qt::AlignVCenter);

        if (song.path == m_currentSong)
        {
            QFont bold = item->font(COL_NAME);
            bold.setBold(true);
            item->setFont(COL_NAME, bold);
            current = item;
        }
    }

    m_songs->setSortingEnabled(true);
    // recent songs in the order they were opened, the others by name
    QTreeWidgetItem *source = m_sources->currentItem();
    if (source && source->data(0, KIND_ROLE).toInt() == SOURCE_RECENT)
        m_songs->sortByColumn(COL_LAST_OPENED, Qt::DescendingOrder);
    else
        m_songs->sortByColumn(COL_NAME, Qt::AscendingOrder);
    for (int column : { COL_DURATION, COL_BARS, COL_TIME_SIG, COL_PARTS, COL_LAST_OPENED, COL_TIMES_OPENED })
        m_songs->resizeColumnToContents(column);

    if (current)
    {
        m_songs->setCurrentItem(current);
        m_songs->scrollToItem(current);
    }
    filterSongs();
    updateButtons();
}

void GuiLibraryDialog::songClicked(QTreeWidgetItem *item, int column)
{
    if (!item || column != COL_FAVOURITE)
        return;
    const bool favourite = item->text(COL_FAVOURITE) != STAR;
    m_library->setFavourite(item->data(0, PATH_ROLE).toString(), favourite);
    item->setText(COL_FAVOURITE, favourite ? STAR : EMPTY_STAR);
    item->setData(COL_FAVOURITE, SORT_ROLE, favourite ? 0 : 1);
}

void GuiLibraryDialog::filterSongs()
{
    const QString text = m_search->text().trimmed();
    for (int i = 0; i < m_songs->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem *item = m_songs->topLevelItem(i);
        const bool matches = text.isEmpty() ||
                             item->text(COL_NAME).contains(text, Qt::CaseInsensitive) ||
                             item->text(COL_FOLDER).contains(text, Qt::CaseInsensitive);
        item->setHidden(!matches);
    }
}

void GuiLibraryDialog::openSelected()
{
    QTreeWidgetItem *item = m_songs->currentItem();
    if (!item || item->isHidden())
        return;
    m_selectedSong = item->data(0, PATH_ROLE).toString();
    accept();
}

void GuiLibraryDialog::addLocation()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Add a Folder with MIDI Files"), QDir::homePath());
    if (folder.isEmpty())
        return;
    m_library->addLocation(folder);
    fillSources(CLibrary::normalisePath(folder));
}

void GuiLibraryDialog::removeLocation()
{
    QTreeWidgetItem *item = m_sources->currentItem();
    if (!item || item->data(0, KIND_ROLE).toInt() != SOURCE_LOCATION)
        return;
    const QString path = item->data(0, PATH_ROLE).toString();
    if (QMessageBox::question(this, tr("Remove Location"),
            tr("Remove \"%1\" from the library?\n\nThe files are not deleted, and the songs keep "
               "their favourites and statistics.").arg(QDir::toNativeSeparators(path))) != QMessageBox::Yes)
        return;
    m_library->removeLocation(path);
    fillSources();
}

void GuiLibraryDialog::updateButtons()
{
    QTreeWidgetItem *source = m_sources->currentItem();
    m_removeLocationButton->setEnabled(source && source->data(0, KIND_ROLE).toInt() == SOURCE_LOCATION &&
                                       source->data(0, KIND_ROLE).isValid());
    QTreeWidgetItem *song = m_songs->currentItem();
    m_openButton->setEnabled(song && !song->isHidden());
}
