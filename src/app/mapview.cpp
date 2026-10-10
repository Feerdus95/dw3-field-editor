#include "mapview.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include <QWheelEvent>

MapView::MapView(QWidget *parent) : QGraphicsView(parent) {
    setTransformationAnchor(AnchorUnderMouse);
    setResizeAnchor(AnchorViewCenter);
    setDragMode(NoDrag);
    setMouseTracking(true);
    setBackgroundBrush(QColor(28, 28, 32));
    setRenderHint(QPainter::SmoothPixmapTransform, false);
    setViewportUpdateMode(SmartViewportUpdate);
    /* panned by dragging: scroll bars would only take room from the map, and
       their coming and going would undo a fit */
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

void MapView::fit() {
    if (!scene()) return;
    fitInView(scene()->sceneRect(), Qt::KeepAspectRatio);
    fitted_ = true;
    Q_EMIT zoomChanged(zoom());
}

void MapView::resizeEvent(QResizeEvent *e) {
    QGraphicsView::resizeEvent(e);
    if (fitted_) fit();
}

void MapView::zoomBy(double factor) {
    double z = zoom() * factor;
    if (z < 0.05 || z > 16) return;
    scale(factor, factor);
    fitted_ = false;
    Q_EMIT zoomChanged(zoom());
}

void MapView::setPaintMode(bool on) {
    paintMode_ = on;
    viewport()->setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
}

void MapView::wheelEvent(QWheelEvent *e) {
    zoomBy(e->angleDelta().y() > 0 ? 1.25 : 0.8);
}

void MapView::mousePressEvent(QMouseEvent *e) {
    pressPos_ = e->pos();
    dragged_ = false;
    if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton || (space_ && e->button() == Qt::LeftButton)) {
        panning_ = true;
        last_ = e->pos();
        return;
    }
    if (paintMode_ && e->button() == Qt::LeftButton) {
        painting_ = true;
        Q_EMIT paintPressed(mapToScene(e->pos()), e->modifiers());
        return;
    }
    leftDown_ = e->button() == Qt::LeftButton;
    QGraphicsView::mousePressEvent(e);
}

void MapView::mouseMoveEvent(QMouseEvent *e) {
    if ((e->pos() - pressPos_).manhattanLength() > 3) dragged_ = true;
    if (panning_) {
        if (dragged_) {
            fitted_ = false;
            viewport()->setCursor(Qt::ClosedHandCursor);
        }
        QPoint d = e->pos() - last_;
        last_ = e->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
    }
    QPointF p = mapToScene(e->pos());
    Q_EMIT hovered(p, scene() && scene()->sceneRect().contains(p));
    if (painting_) {
        Q_EMIT paintMoved(p);
        return;
    }
    QGraphicsView::mouseMoveEvent(e);
}

void MapView::mouseReleaseEvent(QMouseEvent *e) {
    if (panning_) {
        panning_ = false;
        viewport()->setCursor(space_ ? Qt::OpenHandCursor : paintMode_ ? Qt::CrossCursor : Qt::ArrowCursor);
        if (e->button() == Qt::RightButton && !dragged_) Q_EMIT contextMenuAt(mapToScene(e->pos()), e->globalPosition().toPoint());
        return;
    }
    if (painting_) {
        painting_ = false;
        Q_EMIT paintReleased(mapToScene(e->pos()));
        return;
    }
    QGraphicsView::mouseReleaseEvent(e);
    if (leftDown_ && e->button() == Qt::LeftButton) {
        leftDown_ = false;
        if (dragged_) Q_EMIT dragEnded();
    }
}

void MapView::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
        space_ = true;
        viewport()->setCursor(Qt::OpenHandCursor);
        return;
    }
    if (e->key() == Qt::Key_Delete) {
        Q_EMIT deletePressed();
        return;
    }
    QGraphicsView::keyPressEvent(e);
}

void MapView::keyReleaseEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
        space_ = false;
        viewport()->setCursor(paintMode_ ? Qt::CrossCursor : Qt::ArrowCursor);
        return;
    }
    QGraphicsView::keyReleaseEvent(e);
}

void MapView::leaveEvent(QEvent *e) {
    Q_EMIT hovered(QPointF(), false);
    QGraphicsView::leaveEvent(e);
}
