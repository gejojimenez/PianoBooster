/*********************************************************************************/
/*!
@file           GuiPianoRoll.h

@brief          The views of the section editor: a piano roll of the whole song that can
                be scrolled and zoomed without playing it, where a stretch of the song
                is selected with the mouse, and an overview of all the song.

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

#ifndef __GUI_PIANO_ROLL_H__
#define __GUI_PIANO_ROLL_H__

#include <QAbstractScrollArea>
#include <QPixmap>
#include <QWidget>

#include "SongMap.h"
#include "Library.h"

// What the start and the end of a selection are rounded to
enum class CSnap { Bar, Beat, HalfBeat, QuarterBeat, None };

// The notes practised when a channel is chosen in the list of instruments, as when
// playing (see CNote::findHand()): those of that channel and, if it is one of the two
// hands, those of the other hand too.
bool isPractisedNote(const CSongNote &note, int practiseChannel);
// The colour of a note: its hand if it is practised, grey if not. Shared by the two views.
QColor pianoRollNoteColor(const CSongNote &note, int practiseChannel, const QPalette &palette);

class CPianoRollView : public QAbstractScrollArea
{
    Q_OBJECT

public:
    explicit CPianoRollView(QWidget *parent = nullptr);

    void setSongMap(const CSongMap *map);
    void setParts(const QVector<CSongPart> &parts, qint64 selectedId);

    // The selection, from start up to (not including) end, in ticks. Empty if end <= start.
    void setSelection(qint64 start, qint64 end);
    qint64 selectionStart() const { return m_selStart; }
    qint64 selectionEnd() const { return m_selEnd; }
    bool hasSelection() const { return m_selEnd > m_selStart; }

    void setSnap(CSnap snap) { m_snap = snap; }
    qint64 snapTick(qint64 tick) const;

    // The playing position, or -1 for none
    void setCursorTick(qint64 tick, bool follow);

    void setShowOtherParts(bool show);
    void setPractiseChannel(int channel);

    // Zoom, as the number of pixels of a quarter note
    void zoomBy(double factor, int anchorX = -1);
    void zoomToFit(qint64 start, qint64 end);
    void zoomToSong() { zoomToFit(0, m_map ? m_map->endTick() : 0); }
    // Scrolls so that the stretch is seen, if it isn't already
    void ensureVisible(qint64 start, qint64 end);
    void centreOn(qint64 tick);
    qint64 firstVisibleTick() const;
    qint64 lastVisibleTick() const;

signals:
    void selectionChanged(qint64 start, qint64 end); // while it is being dragged too
    void selectionEdited();                           // when the mouse is released
    void newSelectionStarted();                       // dragging away from the edges
    void partClicked(qint64 id);
    void viewChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    enum class Drag { None, Selection };

    static constexpr int KEYS_WIDTH = 40;
    static constexpr int RULER_HEIGHT = 22;
    static constexpr int LANE_HEIGHT = 20;
    static constexpr int HANDLE_DISTANCE = 6;

    int gridTop() const { return RULER_HEIGHT + LANE_HEIGHT; }
    double pixelsPerTick() const;
    double xOfTick(qint64 tick) const;
    qint64 tickOfX(double x) const;
    void updateScrollBar();
    void drawGrid(QPainter &painter, qint64 firstTick, qint64 lastTick);
    void drawKeys(QPainter &painter, int lowNote, int highNote, double rowHeight);
    const CSongPart *partAt(const QPoint &point) const;

    const CSongMap *m_map = nullptr;
    QVector<CSongPart> m_parts;
    qint64 m_selectedPartId = -1;
    qint64 m_selStart = 0;
    qint64 m_selEnd = 0;
    qint64 m_cursorTick = -1;
    CSnap m_snap = CSnap::Beat;
    double m_pixelsPerQuarter = 40.0;
    bool m_showOtherParts = true;
    int m_practiseChannel = -1;
    int m_lowNote = 48;  // the notes shown, from bottom to top
    int m_highNote = 84;

    Drag m_drag = Drag::None;
    qint64 m_dragAnchor = 0;       // the edge of the selection that is not being moved
    qint64 m_savedSelStart = 0;    // the selection before the mouse was pressed
    qint64 m_savedSelEnd = 0;
    bool m_dragMoved = false;
};

class CSongOverview : public QWidget
{
    Q_OBJECT

public:
    explicit CSongOverview(QWidget *parent = nullptr);

    void setSongMap(const CSongMap *map);
    void setParts(const QVector<CSongPart> &parts, qint64 selectedId);
    void setSelection(qint64 start, qint64 end);
    void setVisibleRange(qint64 first, qint64 last);
    void setCursorTick(qint64 tick);
    void setShowOtherParts(bool show);
    void setPractiseChannel(int channel);
    QSize sizeHint() const override { return QSize(400, 48); }

signals:
    void centreRequested(qint64 tick);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    double xOfTick(qint64 tick) const;
    qint64 tickOfX(double x) const;

    const CSongMap *m_map = nullptr;
    QVector<CSongPart> m_parts;
    qint64 m_selectedPartId = -1;
    qint64 m_selStart = 0;
    qint64 m_selEnd = 0;
    qint64 m_visibleFirst = 0;
    qint64 m_visibleLast = 0;
    qint64 m_cursorTick = -1;
    bool m_showOtherParts = true;
    int m_practiseChannel = -1;
    QPixmap m_cache; // the notes, drawn once for each size
};

#endif // __GUI_PIANO_ROLL_H__
