/*********************************************************************************/
/*!
@file           GuiPianoRoll.cpp

@brief          The views of the section editor: the piano roll and the overview.

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

#include "GuiPianoRoll.h"
#include "Chord.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {

constexpr int DRUM_CHANNEL = 9;
constexpr int MIN_NOTE_SPAN = 24; // show at least two octaves

bool isBlackKey(int note)
{
    switch (note % 12)
    {
    case 1: case 3: case 6: case 8: case 10:
        return true;
    default:
        return false;
    }
}

QColor blend(const QColor &a, const QColor &b, double amount)
{
    return QColor::fromRgbF(static_cast<float>(a.redF() + (b.redF() - a.redF()) * amount),
                            static_cast<float>(a.greenF() + (b.greenF() - a.greenF()) * amount),
                            static_cast<float>(a.blueF() + (b.blueF() - a.blueF()) * amount));
}

bool isDark(const QPalette &palette)
{
    return palette.color(QPalette::Base).lightnessF() < 0.5;
}

// The notes shown: those practised, and the others if wanted (but not the drums, unless
// they are the instrument practised)
bool isShown(const CSongNote &note, int practiseChannel, bool showOtherParts)
{
    if (isPractisedNote(note, practiseChannel))
        return true;
    return showOtherParts && note.channel != DRUM_CHANNEL;
}

// The range of the notes shown, with a margin and at least MIN_NOTE_SPAN notes
void noteRange(const CSongMap *map, int practiseChannel, bool showOtherParts, int &low, int &high)
{
    bool any = false;
    if (map)
    {
        for (const CSongNote &note : map->notes())
        {
            if (!isShown(note, practiseChannel, showOtherParts))
                continue;
            low = any ? std::min(low, note.note) : note.note;
            high = any ? std::max(high, note.note) : note.note;
            any = true;
        }
    }
    if (!any)
    {
        low = 60;
        high = 72;
    }
    low -= 2;
    high += 2;
    if (high - low + 1 < MIN_NOTE_SPAN)
    {
        const int middle = (low + high) / 2;
        low = middle - MIN_NOTE_SPAN / 2;
        high = low + MIN_NOTE_SPAN - 1;
    }
    low = std::max(low, 0);
    high = std::min(high, 127);
}

// The colours of the sections in the lane, one after the other
QColor partColor(int index)
{
    static const QColor colors[] = { QColor(230, 160, 40), QColor(170, 110, 210), QColor(40, 170, 170),
                                     QColor(220, 100, 110), QColor(120, 160, 60) };
    return colors[index % (sizeof(colors) / sizeof(colors[0]))];
}

} // namespace

// Without a channel chosen, the piano part
static int effectiveChannel(int practiseChannel)
{
    if (practiseChannel >= 0)
        return practiseChannel;
    return (CNote::rightHandChan() >= 0) ? CNote::rightHandChan() : CNote::leftHandChan();
}

bool isPractisedNote(const CSongNote &note, int practiseChannel)
{
    const int channel = effectiveChannel(practiseChannel);
    if (channel < 0)
        return false;
    if (note.channel == channel)
        return true;
    // the other hand of the piano part
    return CNote::hasPianoPart(channel) && CNote::hasPianoPart(note.channel);
}

QColor pianoRollNoteColor(const CSongNote &note, int practiseChannel, const QPalette &palette)
{
    const QColor right = isDark(palette) ? QColor(90, 160, 255) : QColor(40, 110, 215);
    const QColor left = isDark(palette) ? QColor(80, 200, 120) : QColor(35, 150, 80);
    const QColor other = isDark(palette) ? QColor(130, 130, 130) : QColor(165, 165, 165);

    if (!isPractisedNote(note, practiseChannel))
        return other;

    // the hand, as in CNote::findHand()
    const int channel = effectiveChannel(practiseChannel);
    const int rightTrack = CNote::rightHandTrack(note.channel);
    if (note.channel == channel && rightTrack >= 0)
        return note.track == rightTrack ? right : left; // both hands in one channel, in two tracks
    if (note.channel == CNote::rightHandChan())
        return right;
    if (note.channel == CNote::leftHandChan())
        return left;
    return note.note >= MIDDLE_C ? right : left; // split at middle C
}

/////////////////////////////////////////////////////////////////////////////////////////
// CPianoRollView

CPianoRollView::CPianoRollView(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    viewport()->setMouseTracking(true);
    setMinimumHeight(200);
    setFocusPolicy(Qt::StrongFocus);
}

void CPianoRollView::setSongMap(const CSongMap *map)
{
    m_map = map;
    m_selStart = m_selEnd = 0;
    m_cursorTick = -1;
    noteRange(m_map, m_practiseChannel, m_showOtherParts, m_lowNote, m_highNote);
    updateScrollBar();
    viewport()->update();
}

void CPianoRollView::setParts(const QVector<CSongPart> &parts, qint64 selectedId)
{
    m_parts = parts;
    m_selectedPartId = selectedId;
    viewport()->update();
}

void CPianoRollView::setSelection(qint64 start, qint64 end)
{
    m_selStart = start;
    m_selEnd = end;
    viewport()->update();
}

void CPianoRollView::setShowOtherParts(bool show)
{
    m_showOtherParts = show;
    noteRange(m_map, m_practiseChannel, m_showOtherParts, m_lowNote, m_highNote);
    viewport()->update();
}

void CPianoRollView::setPractiseChannel(int channel)
{
    m_practiseChannel = channel;
    noteRange(m_map, m_practiseChannel, m_showOtherParts, m_lowNote, m_highNote);
    viewport()->update();
}

double CPianoRollView::pixelsPerTick() const
{
    return m_pixelsPerQuarter / (m_map ? m_map->ppqn() : 96);
}

double CPianoRollView::xOfTick(qint64 tick) const
{
    return KEYS_WIDTH + static_cast<double>(tick) * pixelsPerTick() - horizontalScrollBar()->value();
}

qint64 CPianoRollView::tickOfX(double x) const
{
    return static_cast<qint64>(std::floor((x - KEYS_WIDTH + horizontalScrollBar()->value()) / pixelsPerTick()));
}

qint64 CPianoRollView::firstVisibleTick() const
{
    return std::max<qint64>(0, tickOfX(KEYS_WIDTH));
}

qint64 CPianoRollView::lastVisibleTick() const
{
    return tickOfX(viewport()->width());
}

void CPianoRollView::updateScrollBar()
{
    const qint64 endTick = m_map ? m_map->endTick() : 0;
    const int viewWidth = std::max(1, viewport()->width() - KEYS_WIDTH);
    const double contentWidth = static_cast<double>(endTick) * pixelsPerTick() + 40.0;
    QScrollBar *bar = horizontalScrollBar();
    bar->setRange(0, std::max(0, static_cast<int>(contentWidth) - viewWidth));
    bar->setPageStep(viewWidth);
    bar->setSingleStep(std::max(1, viewWidth / 20));
}

qint64 CPianoRollView::snapTick(qint64 tick) const
{
    if (!m_map)
        return tick;
    const qint64 endTick = m_map->endTick();
    tick = std::clamp<qint64>(tick, 0, endTick);
    if (m_snap == CSnap::None)
        return tick;

    const int bar = m_map->barOfTick(tick);
    const qint64 barStart = m_map->barStartTick(bar);
    qint64 unit = m_map->beatTicks(bar);
    switch (m_snap)
    {
    case CSnap::Bar:         unit = m_map->barTicks(bar); break;
    case CSnap::HalfBeat:    unit /= 2; break;
    case CSnap::QuarterBeat: unit /= 4; break;
    default: break;
    }
    unit = std::max<qint64>(unit, 1);
    const qint64 steps = std::llround(static_cast<double>(tick - barStart) / static_cast<double>(unit));
    return std::clamp<qint64>(barStart + steps * unit, 0, endTick);
}

void CPianoRollView::zoomBy(double factor, int anchorX)
{
    if (!m_map)
        return;
    if (anchorX < KEYS_WIDTH)
        anchorX = KEYS_WIDTH + (viewport()->width() - KEYS_WIDTH) / 2;
    const double anchorTick = (anchorX - KEYS_WIDTH + horizontalScrollBar()->value()) / pixelsPerTick();
    m_pixelsPerQuarter = std::clamp(m_pixelsPerQuarter * factor, 0.2, 600.0);
    updateScrollBar();
    horizontalScrollBar()->setValue(static_cast<int>(anchorTick * pixelsPerTick()) - (anchorX - KEYS_WIDTH));
    viewport()->update();
    emit viewChanged();
}

void CPianoRollView::zoomToFit(qint64 start, qint64 end)
{
    if (!m_map || end <= start)
        return;
    const double width = std::max(50, viewport()->width() - KEYS_WIDTH - 30);
    const double quarters = static_cast<double>(end - start) / m_map->ppqn();
    m_pixelsPerQuarter = std::clamp(width / quarters, 0.2, 600.0);
    updateScrollBar();
    horizontalScrollBar()->setValue(static_cast<int>(static_cast<double>(start) * pixelsPerTick()) - 15);
    viewport()->update();
    emit viewChanged();
}

void CPianoRollView::ensureVisible(qint64 start, qint64 end)
{
    if (!m_map)
        return;
    if (start >= firstVisibleTick() && end <= lastVisibleTick())
        return;
    const double width = viewport()->width() - KEYS_WIDTH;
    if (static_cast<double>(end - start) * pixelsPerTick() > width * 0.9)
        zoomToFit(start, end);
    else
        horizontalScrollBar()->setValue(static_cast<int>(static_cast<double>(start) * pixelsPerTick() - width * 0.1));
}

void CPianoRollView::centreOn(qint64 tick)
{
    const double width = viewport()->width() - KEYS_WIDTH;
    horizontalScrollBar()->setValue(static_cast<int>(static_cast<double>(tick) * pixelsPerTick() - width / 2));
}

void CPianoRollView::setCursorTick(qint64 tick, bool follow)
{
    m_cursorTick = tick;
    if (follow && tick >= 0)
    {
        const double width = viewport()->width() - KEYS_WIDTH;
        const double x = xOfTick(tick);
        if (x < KEYS_WIDTH || x > viewport()->width() - width * 0.05)
            horizontalScrollBar()->setValue(static_cast<int>(static_cast<double>(tick) * pixelsPerTick() - width * 0.1));
    }
    viewport()->update();
}

void CPianoRollView::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBar();
    emit viewChanged();
}

void CPianoRollView::scrollContentsBy(int dx, int dy)
{
    Q_UNUSED(dx)
    Q_UNUSED(dy)
    viewport()->update();
    emit viewChanged();
}

void CPianoRollView::drawGrid(QPainter &painter, qint64 firstTick, qint64 lastTick)
{
    const QPalette &pal = palette();
    const QColor barLine = blend(pal.color(QPalette::Base), pal.color(QPalette::Text), 0.45);
    const QColor beatLine = blend(pal.color(QPalette::Base), pal.color(QPalette::Text), 0.15);
    const int height = viewport()->height();

    const int lastBar = std::min(m_map->barOfTick(lastTick) + 1, m_map->barCount());
    for (int bar = m_map->barOfTick(firstTick); bar <= lastBar; ++bar)
    {
        const qint64 barStart = m_map->barStartTick(bar);
        const qint64 beat = m_map->beatTicks(bar);
        if (static_cast<double>(beat) * pixelsPerTick() >= 8.0 && bar < m_map->barCount())
        {
            painter.setPen(beatLine);
            for (int i = 1; i < m_map->beatsInBar(bar); ++i)
            {
                const double x = xOfTick(barStart + i * beat);
                painter.drawLine(QPointF(x, gridTop()), QPointF(x, height));
            }
        }
        painter.setPen(barLine);
        const double x = xOfTick(barStart);
        painter.drawLine(QPointF(x, gridTop()), QPointF(x, height));
    }
}

void CPianoRollView::drawKeys(QPainter &painter, int lowNote, int highNote, double rowHeight)
{
    const QPalette &pal = palette();
    const int height = viewport()->height();
    painter.fillRect(QRect(0, gridTop(), KEYS_WIDTH, height - gridTop()), isDark(pal) ? QColor(200, 200, 200) : Qt::white);
    QFont font = painter.font();
    font.setPointSizeF(std::max(6.0, std::min(8.0, rowHeight * 0.9)));
    painter.setFont(font);
    for (int note = lowNote; note <= highNote; ++note)
    {
        const double y = gridTop() + (highNote - note) * rowHeight;
        if (isBlackKey(note))
            painter.fillRect(QRectF(0, y, KEYS_WIDTH * 0.6, rowHeight), QColor(40, 40, 40));
        if (note % 12 == 0)
        {
            painter.setPen(QColor(120, 120, 120));
            painter.drawLine(QPointF(0, y + rowHeight), QPointF(KEYS_WIDTH, y + rowHeight));
            if (rowHeight * 12 >= 30)
            {
                painter.setPen(Qt::black);
                painter.drawText(QRectF(0, y - 10, KEYS_WIDTH - 3, rowHeight + 10), Qt::AlignRight | Qt::AlignBottom,
                                 QStringLiteral("C%1").arg(note / 12 - 1));
            }
        }
    }
    painter.setPen(pal.color(QPalette::Mid));
    painter.drawLine(KEYS_WIDTH - 1, gridTop(), KEYS_WIDTH - 1, height);
}

void CPianoRollView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(viewport());
    const QPalette &pal = palette();
    const int width = viewport()->width();
    const int height = viewport()->height();
    painter.fillRect(rect(), pal.color(QPalette::Base));
    if (!m_map || !m_map->isValid())
        return;

    const int lowNote = m_lowNote;
    const int highNote = m_highNote;
    const double rowHeight = static_cast<double>(height - gridTop()) / (highNote - lowNote + 1);
    const qint64 firstTick = firstVisibleTick();
    const qint64 lastTick = lastVisibleTick();

    painter.save();
    painter.setClipRect(QRect(KEYS_WIDTH, 0, width - KEYS_WIDTH, height));

    // the rows of the black keys
    const QColor blackRow = blend(pal.color(QPalette::Base), pal.color(QPalette::Text), 0.06);
    for (int note = lowNote; note <= highNote; ++note)
        if (isBlackKey(note))
            painter.fillRect(QRectF(KEYS_WIDTH, gridTop() + (highNote - note) * rowHeight, width - KEYS_WIDTH, rowHeight), blackRow);

    // after the end of the song
    const double endX = xOfTick(m_map->endTick());
    if (endX < width)
        painter.fillRect(QRectF(endX, gridTop(), width - endX, height - gridTop()), pal.color(QPalette::Window));

    // the selection
    const QColor highlight = pal.color(QPalette::Highlight);
    const double selX1 = xOfTick(m_selStart);
    const double selX2 = xOfTick(m_selEnd);
    if (hasSelection())
    {
        QColor band = highlight;
        band.setAlphaF(0.18f);
        painter.fillRect(QRectF(selX1, gridTop(), selX2 - selX1, height - gridTop()), band);
    }

    drawGrid(painter, firstTick, lastTick);

    // the notes, dimmed outside of the selection: the other instruments first, so that
    // the notes practised are drawn over them
    const QVector<CSongNote> &notes = m_map->notes();
    const qint64 searchFrom = firstTick - m_map->maxNoteLength();
    const auto first = std::lower_bound(notes.begin(), notes.end(), searchFrom,
                                        [](const CSongNote &note, qint64 tick) { return note.tick < tick; });
    const QColor outline = isDark(pal) ? QColor(0, 0, 0, 120) : QColor(0, 0, 0, 90);
    for (int pass = 0; pass < 2; ++pass)
    for (auto it = first; it != notes.end() && it->tick <= lastTick; ++it)
    {
        const CSongNote &note = *it;
        if (isPractisedNote(note, m_practiseChannel) != (pass == 1))
            continue;
        if (note.tick + note.length < firstTick || !isShown(note, m_practiseChannel, m_showOtherParts))
            continue;
        if (note.note < lowNote || note.note > highNote)
            continue;
        QColor color = pianoRollNoteColor(note, m_practiseChannel, pal);
        if (hasSelection() && (note.tick < m_selStart || note.tick >= m_selEnd))
            color = blend(color, pal.color(QPalette::Base), 0.6);
        const double x1 = xOfTick(note.tick);
        const double x2 = std::max(xOfTick(note.tick + note.length), x1 + 2.0);
        const double y = gridTop() + (highNote - note.note) * rowHeight;
        const QRectF box(x1, y + 0.5, x2 - x1 - 0.5, std::max(1.0, rowHeight - 1.0));
        painter.fillRect(box, color);
        if (rowHeight >= 4 && x2 - x1 >= 4)
        {
            painter.setPen(outline);
            painter.drawRect(box);
        }
    }

    // the edges of the selection
    if (hasSelection())
    {
        painter.setPen(QPen(highlight, 2));
        painter.drawLine(QPointF(selX1, gridTop()), QPointF(selX1, height));
        painter.drawLine(QPointF(selX2, gridTop()), QPointF(selX2, height));
    }

    // the playing position
    if (m_cursorTick >= 0)
    {
        painter.setPen(QPen(QColor(220, 40, 40), 2));
        const double x = xOfTick(m_cursorTick);
        painter.drawLine(QPointF(x, 0), QPointF(x, height));
    }

    // the ruler with the bar numbers
    painter.fillRect(QRect(KEYS_WIDTH, 0, width - KEYS_WIDTH, RULER_HEIGHT), pal.color(QPalette::Button));
    {
        QFont font = painter.font();
        font.setPointSizeF(8.0);
        painter.setFont(font);
        const QFontMetrics metrics(font);
        const double barWidth = static_cast<double>(m_map->barTicks(m_map->barOfTick(firstTick))) * pixelsPerTick();
        const int labelWidth = metrics.horizontalAdvance(QStringLiteral("8888 12/8")) / 2 + 8;
        int every = 1;
        while (every * barWidth < labelWidth && every < 4096)
            every *= 2;
        const int lastBar = std::min(m_map->barOfTick(lastTick) + 1, m_map->barCount());
        for (int bar = m_map->barOfTick(firstTick); bar <= lastBar; ++bar)
        {
            const double x = xOfTick(m_map->barStartTick(bar));
            const bool sigChange = bar == 0 || m_map->beatsInBar(bar) != m_map->beatsInBar(bar - 1) ||
                                   m_map->timeSigBottom(bar) != m_map->timeSigBottom(bar - 1);
            painter.setPen(pal.color(QPalette::ButtonText));
            painter.drawLine(QPointF(x, RULER_HEIGHT - (bar % every == 0 ? 8 : 4)), QPointF(x, RULER_HEIGHT));
            if (bar % every != 0 && !sigChange)
                continue;
            if (bar >= m_map->barCount())
                continue;
            QString label = QString::number(bar + FIRST_BAR_NUMBER);
            if (sigChange && barWidth >= labelWidth)
                label += QStringLiteral("  %1/%2").arg(m_map->beatsInBar(bar)).arg(m_map->timeSigBottom(bar));
            painter.drawText(QPointF(x + 3, RULER_HEIGHT - 8), label);
        }
        painter.setPen(pal.color(QPalette::Mid));
        painter.drawLine(KEYS_WIDTH, RULER_HEIGHT - 1, width, RULER_HEIGHT - 1);
    }

    // the selection in the ruler, with its two handles
    if (hasSelection())
    {
        QColor band = highlight;
        band.setAlphaF(0.35f);
        painter.fillRect(QRectF(selX1, 0, selX2 - selX1, RULER_HEIGHT), band);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(highlight);
        for (double x : { selX1, selX2 })
        {
            QPainterPath handle;
            handle.moveTo(x - 6, 0);
            handle.lineTo(x + 6, 0);
            handle.lineTo(x, 9);
            handle.closeSubpath();
            painter.drawPath(handle);
        }
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setBrush(Qt::NoBrush);
    }

    // the lane with the saved sections
    painter.fillRect(QRect(KEYS_WIDTH, RULER_HEIGHT, width - KEYS_WIDTH, LANE_HEIGHT), pal.color(QPalette::Window));
    {
        QFont font = painter.font();
        font.setPointSizeF(8.0);
        painter.setFont(font);
        for (int i = 0; i < m_parts.size(); ++i)
        {
            const CSongPart &part = m_parts[i];
            const double x1 = xOfTick(m_map->tickOfPosition(part.start));
            const double x2 = xOfTick(m_map->sectionEndTick(part.end));
            if (x2 < KEYS_WIDTH || x1 > width)
                continue;
            const bool selected = part.id == m_selectedPartId;
            QColor color = partColor(i);
            color.setAlphaF(selected ? 0.95f : 0.55f);
            const QRectF box(x1, RULER_HEIGHT + 2, std::max(2.0, x2 - x1), LANE_HEIGHT - 4);
            painter.fillRect(box, color);
            if (selected)
            {
                painter.setPen(QPen(pal.color(QPalette::Text), 1));
                painter.drawRect(box);
            }
            painter.setPen(selected ? Qt::black : pal.color(QPalette::WindowText));
            const QRectF textBox = box.adjusted(3, 0, -2, 0).intersected(QRectF(KEYS_WIDTH + 2, 0, width, height));
            painter.drawText(textBox, Qt::AlignLeft | Qt::AlignVCenter,
                             painter.fontMetrics().elidedText(part.name, Qt::ElideRight, static_cast<int>(textBox.width())));
        }
        painter.setPen(pal.color(QPalette::Mid));
        painter.drawLine(KEYS_WIDTH, gridTop() - 1, width, gridTop() - 1);
    }
    painter.restore();

    drawKeys(painter, lowNote, highNote, rowHeight);
    painter.fillRect(QRect(0, 0, KEYS_WIDTH, gridTop()), pal.color(QPalette::Button));
}

const CSongPart *CPianoRollView::partAt(const QPoint &point) const
{
    if (!m_map || point.y() < RULER_HEIGHT || point.y() >= gridTop())
        return nullptr;
    const CSongPart *found = nullptr;
    qint64 foundLength = 0;
    for (const CSongPart &part : m_parts)
    {
        const qint64 start = m_map->tickOfPosition(part.start);
        const qint64 end = m_map->sectionEndTick(part.end);
        if (point.x() < xOfTick(start) || point.x() > std::max(xOfTick(end), xOfTick(start) + 2))
            continue;
        if (!found || end - start < foundLength) // the shortest one, if they overlap
        {
            found = &part;
            foundLength = end - start;
        }
    }
    return found;
}

void CPianoRollView::mousePressEvent(QMouseEvent *event)
{
    if (!m_map || event->button() != Qt::LeftButton || event->position().x() < KEYS_WIDTH)
        return;
    const QPoint point = event->position().toPoint();
    if (const CSongPart *part = partAt(point))
    {
        emit partClicked(part->id);
        return;
    }

    m_savedSelStart = m_selStart;
    m_savedSelEnd = m_selEnd;
    m_dragMoved = false;
    m_drag = Drag::Selection;
    const double x = event->position().x();
    const double startDistance = std::abs(x - xOfTick(m_selStart));
    const double endDistance = std::abs(x - xOfTick(m_selEnd));
    if (hasSelection() && std::min(startDistance, endDistance) <= HANDLE_DISTANCE)
    {
        // move the nearest edge; the other one stays where it is
        m_dragAnchor = (startDistance <= endDistance) ? m_selEnd : m_selStart;
        return;
    }
    m_dragAnchor = snapTick(tickOfX(x));
    m_selStart = m_selEnd = m_dragAnchor;
    viewport()->update();
    emit newSelectionStarted();
}

void CPianoRollView::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_map)
        return;
    const double x = event->position().x();
    if (m_drag == Drag::None)
    {
        const QPoint point = event->position().toPoint();
        const bool nearEdge = hasSelection() && point.y() >= 0 && x >= KEYS_WIDTH &&
            (std::abs(x - xOfTick(m_selStart)) <= HANDLE_DISTANCE || std::abs(x - xOfTick(m_selEnd)) <= HANDLE_DISTANCE);
        if (nearEdge && !partAt(point))
            viewport()->setCursor(Qt::SizeHorCursor);
        else if (partAt(point))
            viewport()->setCursor(Qt::PointingHandCursor);
        else
            viewport()->unsetCursor();
        return;
    }

    // scroll when the mouse goes past the sides
    QScrollBar *bar = horizontalScrollBar();
    if (x > viewport()->width() - 10)
        bar->setValue(bar->value() + 20);
    else if (x < KEYS_WIDTH + 10)
        bar->setValue(bar->value() - 20);

    const qint64 tick = snapTick(tickOfX(x));
    const qint64 start = std::min(m_dragAnchor, tick);
    const qint64 end = std::max(m_dragAnchor, tick);
    if (start != m_selStart || end != m_selEnd)
    {
        m_selStart = start;
        m_selEnd = end;
        m_dragMoved = true;
        viewport()->update();
        emit selectionChanged(m_selStart, m_selEnd);
    }
}

void CPianoRollView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || m_drag == Drag::None)
        return;
    m_drag = Drag::None;
    if (!hasSelection())
    {
        // a click without dragging keeps the selection there was
        m_selStart = m_savedSelStart;
        m_selEnd = m_savedSelEnd;
        viewport()->update();
        emit selectionChanged(m_selStart, m_selEnd);
        return;
    }
    if (m_dragMoved)
        emit selectionEdited();
}

void CPianoRollView::wheelEvent(QWheelEvent *event)
{
    const QPoint delta = event->angleDelta();
    if (event->modifiers() & Qt::ControlModifier)
    {
        zoomBy(std::pow(1.0015, delta.y()), static_cast<int>(event->position().x()));
    }
    else
    {
        const int steps = (delta.x() != 0) ? delta.x() : delta.y();
        QScrollBar *bar = horizontalScrollBar();
        bar->setValue(bar->value() - steps * bar->pageStep() / 600);
    }
    event->accept();
}

/////////////////////////////////////////////////////////////////////////////////////////
// CSongOverview

CSongOverview::CSongOverview(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(40);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("The whole song: click or drag to go to a place"));
}

void CSongOverview::setSongMap(const CSongMap *map)
{
    m_map = map;
    m_cache = QPixmap();
    update();
}

void CSongOverview::setParts(const QVector<CSongPart> &parts, qint64 selectedId)
{
    m_parts = parts;
    m_selectedPartId = selectedId;
    update();
}

void CSongOverview::setSelection(qint64 start, qint64 end)
{
    m_selStart = start;
    m_selEnd = end;
    update();
}

void CSongOverview::setVisibleRange(qint64 first, qint64 last)
{
    m_visibleFirst = first;
    m_visibleLast = last;
    update();
}

void CSongOverview::setCursorTick(qint64 tick)
{
    m_cursorTick = tick;
    update();
}

void CSongOverview::setShowOtherParts(bool show)
{
    m_showOtherParts = show;
    m_cache = QPixmap();
    update();
}

void CSongOverview::setPractiseChannel(int channel)
{
    m_practiseChannel = channel;
    m_cache = QPixmap();
    update();
}

double CSongOverview::xOfTick(qint64 tick) const
{
    const qint64 end = (m_map && m_map->endTick() > 0) ? m_map->endTick() : 1;
    return static_cast<double>(tick) * (width() - 1) / static_cast<double>(end);
}

qint64 CSongOverview::tickOfX(double x) const
{
    const qint64 end = (m_map && m_map->endTick() > 0) ? m_map->endTick() : 1;
    return static_cast<qint64>(x * static_cast<double>(end) / std::max(1, width() - 1));
}

void CSongOverview::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    const QPalette &pal = palette();
    if (!m_map || !m_map->isValid())
    {
        painter.fillRect(rect(), pal.color(QPalette::Base));
        return;
    }

    const int laneHeight = 7;
    // the notes do not change, so they are drawn once for each size
    if (m_cache.size() != size() * devicePixelRatioF())
    {
        m_cache = QPixmap(size() * devicePixelRatioF());
        m_cache.setDevicePixelRatio(devicePixelRatioF());
        m_cache.fill(pal.color(QPalette::Base));
        QPainter cache(&m_cache);
        int lowNote, highNote;
        noteRange(m_map, m_practiseChannel, m_showOtherParts, lowNote, highNote);
        const double rowHeight = static_cast<double>(height() - laneHeight - 2) / (highNote - lowNote + 1);
        // the others first, so that the notes practised are drawn over them
        for (int pass = 0; pass < 2; ++pass)
        {
            for (const CSongNote &note : m_map->notes())
            {
                if (isPractisedNote(note, m_practiseChannel) != (pass == 1))
                    continue;
                if (!isShown(note, m_practiseChannel, m_showOtherParts) || note.note < lowNote || note.note > highNote)
                    continue;
                const double x1 = xOfTick(note.tick);
                const double x2 = std::max(xOfTick(note.tick + note.length), x1 + 1.0);
                const double y = laneHeight + 1 + (highNote - note.note) * rowHeight;
                cache.fillRect(QRectF(x1, y, x2 - x1, std::max(1.0, rowHeight)), pianoRollNoteColor(note, m_practiseChannel, pal));
            }
        }
    }
    painter.drawPixmap(0, 0, m_cache);

    // the saved sections, at the top
    painter.fillRect(QRect(0, 0, width(), laneHeight), pal.color(QPalette::Window));
    for (int i = 0; i < m_parts.size(); ++i)
    {
        const double x1 = xOfTick(m_map->tickOfPosition(m_parts[i].start));
        const double x2 = xOfTick(m_map->sectionEndTick(m_parts[i].end));
        QColor color = partColor(i);
        color.setAlphaF(m_parts[i].id == m_selectedPartId ? 1.0f : 0.6f);
        painter.fillRect(QRectF(x1, 1, std::max(2.0, x2 - x1), laneHeight - 2), color);
    }

    const QColor highlight = pal.color(QPalette::Highlight);
    if (m_selEnd > m_selStart)
    {
        QColor band = highlight;
        band.setAlphaF(0.25f);
        painter.fillRect(QRectF(xOfTick(m_selStart), laneHeight, xOfTick(m_selEnd) - xOfTick(m_selStart), height() - laneHeight), band);
    }

    // the part seen in the piano roll
    const double x1 = xOfTick(m_visibleFirst);
    const double x2 = std::max(xOfTick(m_visibleLast), x1 + 3.0);
    painter.setPen(QPen(pal.color(QPalette::Text), 1));
    painter.drawRect(QRectF(x1, 0.5, x2 - x1, height() - 1.5));

    if (m_cursorTick >= 0)
    {
        painter.setPen(QPen(QColor(220, 40, 40), 2));
        const double x = xOfTick(m_cursorTick);
        painter.drawLine(QPointF(x, 0), QPointF(x, height()));
    }
    painter.setPen(pal.color(QPalette::Mid));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
}

void CSongOverview::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        emit centreRequested(tickOfX(event->position().x()));
}

void CSongOverview::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton)
        emit centreRequested(tickOfX(event->position().x()));
}
