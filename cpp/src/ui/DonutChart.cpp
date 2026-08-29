#include "ui/DonutChart.h"

#include <QPainter>
#include <QPen>

DonutChart::DonutChart(QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(96, 96);
    setAttribute(Qt::WA_TranslucentBackground);
}

void DonutChart::setData(int ok, int total)
{
    m_ok = ok;
    m_total = total;
    update();
}

QSize DonutChart::minimumSizeHint() const
{
    return QSize(96, 96);
}

void DonutChart::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int side = qMin(width(), height());
    const QRectF rect(4, 4, side - 8, side - 8);
    const int penW = 12;

    // Vòng nền.
    p.setPen(QPen(QColor(QStringLiteral("#e8eaed")), penW, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(rect, 0, 360 * 16);

    // Vòng tỷ lệ thành công (bắt đầu từ 12 giờ, theo chiều kim đồng hồ).
    if (m_total > 0 && m_ok > 0) {
        const double ratio = double(m_ok) / m_total;
        const int startAngle = 90 * 16;
        const int span = -int(ratio * 360 * 16);
        p.setPen(QPen(QColor(QStringLiteral("#34a853")), penW, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(rect, startAngle, span);
    }

    // Phần trăm ở giữa.
    const QString text = m_total > 0
                             ? QStringLiteral("%1%").arg(int(m_ok * 100.0 / m_total))
                             : QStringLiteral("–");
    QFont f = p.font();
    f.setPointSizeF(13);
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(QStringLiteral("#202124")));
    p.drawText(rect.adjusted(-6, -2, -6, -2), Qt::AlignCenter, text);

    // Nhãn nhỏ.
    f.setPointSizeF(7);
    f.setBold(false);
    p.setFont(f);
    p.setPen(QColor(QStringLiteral("#5f6368")));
    p.drawText(rect.adjusted(-6, 6, -6, 14), Qt::AlignCenter, QStringLiteral("thành công"));
}