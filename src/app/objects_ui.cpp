/* The objects of every field, to add to another: each one's frames drawn as
   its own field draws them, then imported into this field's sheet like a
   picture (addObjectFrames) */
#include "mainwindow.h"
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
#include <set>

QImage toQImage(const Image &img);

/* An object's frames on one picture each, its point (where it is drawn)
   at their bottom middle as the sheet import anchors them; below: how far
   the frames reach under the point, which the object is moved down by */
static std::vector<Image> anchored(const std::vector<Sprite> &frames, int *below) {
    int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    for (auto &f : frames)
        if (f.img.w) x0 = std::min(x0, f.ox), x1 = std::max(x1, f.ox + f.img.w), y0 = std::min(y0, f.oy), y1 = std::max(y1, f.oy + f.img.h);
    int half = std::max(-x0, x1), w = 2 * half, h = y1 - y0;
    *below = y1;
    std::vector<Image> out;
    for (auto &f : frames) {
        Image im;
        im.resize(std::max(w, 1), std::max(h, 1));
        for (int y = 0; y < f.img.h; y++)
            for (int x = 0; x < f.img.w; x++) im.px[(size_t)(f.oy - y0 + y) * im.w + half + f.ox + x] = f.img.px[(size_t)y * f.img.w + x];
        out.push_back(im);
    }
    return out;
}

const std::vector<MainWindow::LibraryObject> &MainWindow::objectLibrary() {
    if (!library_.empty()) return library_;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    std::set<std::vector<uint32_t>> seen;
    for (const Stage &st : stages_) {
        if (st.isNew()) continue;
        Field f;
        if (!f.load(files_, st)) continue;
        size_t sheetSize = 0, spriteSize = 0;
        const uint8_t *sheet = archive_entry(f.sheetFile_, st.sheetEntry & 0xFFFF, &sheetSize);
        const uint8_t *sprites = archive_entry(f.spritesFile_, 0, &spriteSize);
        for (int k = 0; k < (int)st.objects.size(); k++) {
            const Object &o = st.objects[k];
            const uint8_t *from = o.anim == 0xFF ? sprites : sheet;
            if (!from) continue;
            int first = o.frame, last = o.frame;
            if (o.cycle && o.cycleLast >= o.cycleFirst) first = o.cycleFirst, last = std::min(o.cycleLast, o.cycleFirst + 31);
            static const int neutral[3] = {128, 128, 128};
            std::vector<Sprite> frames;
            for (int n = first; n <= last; n++)
                frames.push_back(draw_sheet_frame(from, o.anim == 0xFF ? spriteSize : sheetSize, n, f.vram, o.anim == 0xFF ? 0x200 : 0x140,
                                                  0x100, 0, 0x1F0, o.clutRow, neutral));
            if (frames.empty() || !frames[0].img.w) continue;
            /* the same look in many fields (or many times in one): listed once */
            std::vector<uint32_t> key = frames[0].img.px;
            key.push_back((uint32_t)frames.size());
            if (!seen.insert(key).second) continue;
            LibraryObject l;
            l.field = stageTitle(st) + " (" + QString::fromStdString(st.name) + ")";
            l.index = k;
            l.frames = anchored(frames, &l.below);
            l.depth = o.depth, l.margin = o.margin, l.delay = o.cycleDelay ? o.cycleDelay : 8;
            library_.push_back(l);
        }
    }
    QApplication::restoreOverrideCursor();
    return library_;
}

void MainWindow::addLibraryObject(QPointF at) {
    if (field_.stage.name.empty()) return;
    const auto &lib = objectLibrary();
    QDialog d(this);
    d.setWindowTitle("Add an object from another field");
    d.resize(900, 640);
    auto *v = new QVBoxLayout(&d);
    auto *filter = new QLineEdit;
    filter->setPlaceholderText("Search a field");
    auto *list = new QListWidget;
    list->setViewMode(QListView::IconMode);
    list->setIconSize(QSize(96, 96));
    list->setGridSize(QSize(150, 140));
    list->setResizeMode(QListView::Adjust);
    list->setWordWrap(true);
    for (int i = 0; i < (int)lib.size(); i++) {
        const LibraryObject &l = lib[i];
        QImage img = toQImage(l.frames[0]);
        QPixmap pm(96, 96);
        pm.fill(QColor(60, 60, 60));
        QPainter p(&pm);
        QSize fit = img.size().scaled(96, 96, Qt::KeepAspectRatio);
        if (img.width() <= 96 && img.height() <= 96) fit = img.size();
        p.drawImage(QRect((96 - fit.width()) / 2, (96 - fit.height()) / 2, fit.width(), fit.height()), img);
        p.end();
        QString text = l.field + QString("\n#%1").arg(l.index);
        if (l.frames.size() > 1) text += QString(", %1 frames").arg(l.frames.size());
        auto *it = new QListWidgetItem(QIcon(pm), text);
        it->setData(Qt::UserRole, i);
        it->setToolTip(QString("%1, object #%2: %3 × %4 pixels").arg(l.field).arg(l.index).arg(l.frames[0].w).arg(l.frames[0].h));
        list->addItem(it);
    }
    connect(filter, &QLineEdit::textChanged, &d, [list](const QString &t) {
        for (int i = 0; i < list->count(); i++) list->item(i)->setHidden(!list->item(i)->text().contains(t, Qt::CaseInsensitive));
    });
    auto *info = new QLabel(QString("%1 objects from the game's fields. The one you pick is copied into this field's own "
                                    "sprites (at most 15 colors a frame), standing where you clicked.")
                                .arg(lib.size()));
    info->setWordWrap(true);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    v->addWidget(filter);
    v->addWidget(list, 1);
    v->addWidget(info);
    v->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    connect(list, &QListWidget::itemDoubleClicked, &d, &QDialog::accept);
    if (d.exec() != QDialog::Accepted || !list->currentItem()) return;
    const LibraryObject &l = lib[list->currentItem()->data(Qt::UserRole).toInt()];
    addObjectFrames("Add an object from another field", l.frames, QPointF(at.x(), at.y() + l.below), l.delay, l.depth, l.margin);
}
