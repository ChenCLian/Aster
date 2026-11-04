#include "ui/results/record_item_delegate.h"
#include "ui/settings/theme_manager.h"
#include <QPainter>
#include <QDateTime>
#include <QApplication>
#include <QStyleOptionButton>
#include <algorithm>
namespace aster::ui
{
QSize RecordItemDelegate::sizeHint(const QStyleOptionViewItem &o, const QModelIndex &) const
{ return QSize(0, std::max(72, QFontMetrics(o.font).height()*2+32)); }
void RecordItemDelegate::paint(QPainter *p, const QStyleOptionViewItem &o, const QModelIndex &i) const
{
    p->save(); p->setClipRect(o.rect); p->setRenderHint(QPainter::Antialiasing);
    const bool selected = o.state & QStyle::State_Selected;
    const QRect box = o.rect.adjusted(2,3,-2,-3);
    p->setPen(Qt::NoPen);
    if (selected || (o.state & QStyle::State_MouseOver))
    {
        p->setBrush(themeColor(selected ? "selection" : "hover"));
        p->drawRoundedRect(box,4,4);
    }
    if (selected)
    { p->setBrush(themeColor("accent")); p->drawRoundedRect(QRect(box.left(),box.top()+10,3,box.height()-20),1.5,1.5); }
    const bool checkable = i.flags() & Qt::ItemIsUserCheckable;
    if (checkable)
    {
        QStyleOptionButton check;
        check.rect = checkRect(o.rect); check.palette = o.palette;
        check.state = QStyle::State_Enabled | (i.data(Qt::CheckStateRole).toInt()==Qt::Checked ? QStyle::State_On : QStyle::State_Off);
        (o.widget ? o.widget->style() : QApplication::style())->drawPrimitive(QStyle::PE_IndicatorCheckBox,&check,p,o.widget);
    }
    const QRect inner = box.adjusted(checkable ? 38 : 12,9,-12,-9);
    QFont main=o.font; main.setWeight(QFont::DemiBold);
    QFont meta=o.font;
    if (meta.pointSizeF()>0) meta.setPointSizeF(std::max(8., meta.pointSizeF()-1));
    const QFontMetrics mm(meta), fm(main);
    const int topHeight=mm.height()+4;
    const QString id=QString("ID %1").arg(i.data(RecordRole::Id).toLongLong());
    const int badgeWidth=mm.horizontalAdvance(id);
    p->setFont(meta); p->setPen(themeColor(selected ? "accent" : "muted"));
    p->drawText(QRect(inner.left(),inner.top(),badgeWidth,topHeight),Qt::AlignVCenter,id);
    const auto date=i.data(RecordRole::StartedAt).toDateTime().toLocalTime();
    const QRect timeRect(inner.left()+badgeWidth+8,inner.top(),std::max(0,inner.width()-badgeWidth-8),topHeight);
    p->setPen(themeColor("muted"));
    p->drawText(timeRect,Qt::AlignRight|Qt::AlignVCenter,mm.elidedText(date.toString("MM-dd hh:mm"),Qt::ElideRight,timeRect.width()));
    const QString state=i.data(RecordRole::State).toString();
    p->setPen(Qt::NoPen); p->setBrush(themeColor(state=="Succeeded" ? "success" : state=="Failed" ? "error" : "muted"));
    const int y=inner.top()+topHeight+7;
    p->drawEllipse(QPointF(inner.left()+3,y+fm.height()/2.),3,3);
    p->setPen(themeColor("text")); p->setFont(main);
    const QRect nameRect(inner.left()+13,y,std::max(0,inner.width()-13),fm.height());
    p->drawText(nameRect,Qt::AlignVCenter,fm.elidedText(i.data(RecordRole::FileName).toString(),Qt::ElideMiddle,nameRect.width()));
    if (o.state & QStyle::State_HasFocus)
    { p->setPen(QPen(themeColor("accent"),1,Qt::DotLine)); p->setBrush(Qt::NoBrush); p->drawRoundedRect(box.adjusted(1,1,-1,-1),6,6); }
    p->restore();
}
} // namespace aster::ui
