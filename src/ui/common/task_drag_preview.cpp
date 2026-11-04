#include "ui/common/task_drag_preview.h"
#include "ui/common/presentation.h"
#include "ui/settings/theme_manager.h"
#include <QPainter>
#include <QFontMetrics>
#include <algorithm>
#include <cmath>
namespace aster::ui
{
TaskDragPreview taskDragPreview(const TaskDescriptor &task, const QFont &font, qreal dpr)
{
    if (!std::isfinite(dpr) || dpr <= 0)
        dpr = 1;
    dpr = std::min(dpr, qreal(4));
    QFont titleFont = font;
    titleFont.setBold(true);
    const QFontMetrics titleMetrics(titleFont), bodyMetrics(font);
    const int width = 300, padding = 14, iconSize = 22;
    const int height =
        padding * 2 + std::max(iconSize, titleMetrics.height()) + 8 + bodyMetrics.height();
    QPixmap pixmap(int(std::ceil((width + 8) * dpr)), int(std::ceil((height + 8) * dpr)));
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 30));
    p.drawRoundedRect(QRectF(5, 5, width, height), 10, 10);
    auto background = themeColor("surface");
    background.setAlpha(224);
    auto border = themeColor("accent");
    border.setAlpha(210);
    p.setPen(QPen(border, 1.5));
    p.setBrush(background);
    p.drawRoundedRect(QRectF(1, 1, width, height), 10, 10);
    p.setOpacity(.90);
    methodIcon(!task.inputKind).paint(&p, QRect(padding, padding, iconSize, iconSize));
    p.setFont(titleFont);
    p.setPen(themeColor("text"));
    p.drawText(QRect(padding + iconSize + 8, padding, width - 2 * padding - iconSize - 8,
                     titleMetrics.height()),
               Qt::AlignVCenter,
               titleMetrics.elidedText(methodLabel(task), Qt::ElideRight,
                                       width - 2 * padding - iconSize - 8));
    p.setFont(font);
    p.setPen(themeColor("muted"));
    p.drawText(
        QRect(padding, height - padding - bodyMetrics.height(), width - 2 * padding,
              bodyMetrics.height()),
        Qt::AlignVCenter,
        bodyMetrics.elidedText(text(task.inputKind ? "ui.drag_task_hint" : "ui.drag_source_hint"),
                               Qt::ElideRight, width - 2 * padding));
    // Cursor is near the upper-left corner; the card does not conceal a slot's label.
    return {pixmap, QPoint(12, 12)};
}
} // namespace aster::ui
