/* The map: a QGraphicsView that zooms with the wheel around the cursor and
   pans with the middle or right button (or Space and the left one). In
   paint mode the left button paints instead of selecting; a right click
   that doesn't drag asks for the context menu. */
#pragma once
#include <QGraphicsView>

class MapView : public QGraphicsView {
    Q_OBJECT
  public:
    explicit MapView(QWidget *parent = nullptr);
    void fit();
    void zoomBy(double factor);
    double zoom() const { return transform().m11(); }
    void setPaintMode(bool on);

  Q_SIGNALS:
    void hovered(QPointF scenePos, bool inside);
    void zoomChanged(double zoom);
    /* paint mode's strokes, with the keyboard modifiers of their start */
    void paintPressed(QPointF scenePos, Qt::KeyboardModifiers mods);
    void paintMoved(QPointF scenePos);
    void paintReleased(QPointF scenePos);
    void contextMenuAt(QPointF scenePos, QPoint globalPos);
    void dragEnded(); /* a left drag in select mode ended: moved items may need committing */
    void deletePressed();

  protected:
    void wheelEvent(QWheelEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    void leaveEvent(QEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

  private:
    bool panning_ = false, space_ = false, painting_ = false, paintMode_ = false, leftDown_ = false;
    bool fitted_ = false; /* the map follows the window's size until the user zooms or pans */
    QPoint last_, pressPos_;
    bool dragged_ = false;
};
