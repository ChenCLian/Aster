#include "ui/common/parameter_icons.h"
#include "ui/settings/theme_manager.h"
#include <QPainter>
#include <QPixmap>
#include <cmath>
namespace aster::ui {
QIcon parameterIcon(const QString& name)
{
    if(name.isEmpty())return {};
    QPixmap pix(40,40);pix.setDevicePixelRatio(2);pix.fill(Qt::transparent);
    QPainter p(&pix);p.setRenderHint(QPainter::Antialiasing);p.setPen(QPen(themeColor("accent"),1.4));
    if(name=="latitude") {
        p.drawEllipse(QRectF(2,2,16,16));p.drawEllipse(QRectF(6,2,8,16));
        p.drawLine(2,10,18,10);p.drawArc(QRectF(2,5,16,9),0,180*16);
    } else if(name=="calendar") {
        p.drawRoundedRect(QRectF(3,4,14,13),2,2);p.drawLine(3,8,17,8);
        p.drawLine(7,2,7,6);p.drawLine(13,2,13,6);p.drawPoint(7,11);p.drawPoint(12,11);p.drawPoint(7,14);
    } else if(name=="orbit") {
        p.drawEllipse(QRectF(2,5,16,10));p.setBrush(themeColor("accent"));p.drawEllipse(QPointF(7,10),2,2);
    } else if(name=="sun" || name=="season") {
        p.drawEllipse(QRectF(6,6,8,8));
        for(int i=0;i<8;++i){double a=i*3.141592653589793/4;p.drawLine(QPointF(10+6*std::cos(a),10+6*std::sin(a)),QPointF(10+8*std::cos(a),10+8*std::sin(a)));}
    } else return {};
    return QIcon(pix);
}
}
