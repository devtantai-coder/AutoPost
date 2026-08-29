#include "ui/BarChart.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

BarChart::BarChart(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(170);
    setMouseTracking(true);
}

void BarChart::setData(const QStringList &labels, const QVector<int> &totals,
                       const QVector<int> &okValues)
{
    m_labels = labels;
    m_totals = totals;
    m_ok = okValues;
    m_rects.clear();
    m_hoverIndex = -1;
    update();
}

QSize BarChart::minimumSizeHint() const
{
    return QSize(380, 170);
}

void BarChart::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int padL = 8;
    const int padR = 8;
    const int padT = 22;
    const int padB = 26;
    const int w = width();
    const int h = height();
    const int chartW = w - padL - padR;
    const int chartH = h - padT - padB;
    const int baseY = padT + chartH;

    // Lưới ngang mờ.
    p.setPen(QPen(QColor(QStringLiteral("#f1f3f4")), 1));
    for (int gi = 0; gi <= 4; ++gi) {
        const int y = padT + chartH - gi * chartH / 4;
        p.drawLine(padL, y, w - padR, y);
    }

    const int n = m_totals.size();
    if (n == 0) {
        p.setPen(QColor(QStringLiteral("#9aa0a6")));
        p.setFont(QFont(p.font().family(), 12));
        p.drawText(QRect(padL, padT, chartW, chartH), Qt::AlignCenter,
                   QStringLiteral("Chưa có dữ liệu - hãy bắt đầu đăng bài"));
        return;
    }

    int maxVal = 1;
    for (int v : m_totals)
        maxVal = qMax(maxVal, v);

    // Nhãn trục Y theo maxVal.
    p.setPen(QColor(QStringLiteral("#9aa0a6")));
    QFont yf = p.font();
    yf.setPointSizeF(7.5);
    p.setFont(yf);
    for (int gi = 0; gi <= 4; ++gi) {
        const int y = padT + chartH - gi * chartH / 4;
        p.drawText(QRect(0, y - 7, padL - 4, 14), Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(qMax(1, maxVal * gi / 4)));
    }

    const int slot = chartW / n;
    const int barW = qMin(slot - 10, 54);
    const int radius = 4;

    m_rects.clear();

    for (int i = 0; i < n; ++i) {
        const int total = m_totals.at(i);
        const int ok = i < m_ok.size() ? m_ok.at(i) : 0;
        const int fail = qMax(0, total - ok);
        const int x = padL + i * slot + (slot - barW) / 2;
        const bool isToday = (i == n - 1);
        const bool hovered = (i == m_hoverIndex);

        // Nền cột hôm nay / đang hover.
        if (isToday || hovered) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(hovered ? QStringLiteral("#e8f0fe")
                                      : QStringLiteral("#f1f3f4")));
            p.drawRoundedRect(QRect(padL + i * slot + 2, padT, slot - 4, chartH), 8, 8);
        }

        if (total > 0) {
            // Số liệu trên đỉnh cột.
            p.setPen(QColor(hovered ? QStringLiteral("#1a73e8")
                                    : QStringLiteral("#5f6368")));
            QFont f = p.font();
            f.setPointSizeF(8.5);
            f.setBold(hovered || isToday);
            p.setFont(f);
            p.drawText(QRect(x - 4, padT - 16, barW + 8, 14), Qt::AlignCenter,
                       QString::number(total));
        } else {
            // Cột rỗng: một vạch xám.
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(QStringLiteral("#e8eaed")));
            p.drawRoundedRect(QRect(x, baseY - 4, barW, 4), 2, 2);
            m_rects.append({QRect(x, baseY - 4, barW, 4), i});
            continue;
        }

        const int failH = qMax(2, int(double(fail) / maxVal * chartH));
        const int okH = qMax(2, int(double(ok) / maxVal * chartH));
        const int yTop = baseY - failH - okH;

        if (failH > 0) {
            QLinearGradient g(0, yTop, 0, baseY);
            g.setColorAt(0, QColor(QStringLiteral("#f28b82")));
            g.setColorAt(1, QColor(QStringLiteral("#ea4335")));
            p.setPen(Qt::NoPen);
            p.setBrush(g);
            p.drawRoundedRect(QRect(x, yTop + okH, barW, failH), radius, radius);
        }
        if (okH > 0) {
            QLinearGradient g(0, yTop, 0, yTop + okH);
            g.setColorAt(0, QColor(QStringLiteral("#81c995")));
            g.setColorAt(1, QColor(QStringLiteral("#34a853")));
            p.setPen(Qt::NoPen);
            p.setBrush(g);
            p.drawRoundedRect(QRect(x, yTop, barW, okH), radius, radius);
        }

        m_rects.append({QRect(x, yTop, barW, okH + failH), i});

        // Nhãn ngày.
        if (i < m_labels.size()) {
            QFont f = p.font();
            f.setPointSizeF(8.5);
            f.setBold(isToday);
            p.setFont(f);
            p.setPen(QColor(isToday ? QStringLiteral("#202124")
                                    : QStringLiteral("#5f6368")));
            p.drawText(QRect(padL + i * slot, padT + chartH + 6, slot, padB - 8),
                       Qt::AlignCenter, m_labels.at(i));
        }
    }

    // ----- Đường trend tỷ lệ thành công (điểm nối giữa các cột) -----
    {
        QVector<QPointF> points;
        for (int i = 0; i < n; ++i) {
            const int total = m_totals.at(i);
            const int ok = i < m_ok.size() ? m_ok.at(i) : 0;
            if (total <= 0)
                continue;
            const int slot = chartW / n;
            const double cx = padL + i * slot + slot / 2.0;
            const double y = baseY - double(ok) / maxVal * chartH;
            points.append(QPointF(cx, y));
        }
        if (points.size() >= 2) {
            QPainterPath path;
            path.moveTo(points.first());
            for (int i = 1; i < points.size(); ++i)
                path.lineTo(points.at(i));

            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(26, 115, 232, 120), 1.5, Qt::DashLine));
            p.drawPath(path);

            p.setPen(Qt::NoPen);
            p.setBrush(QColor(QStringLiteral("#1a73e8")));
            for (const QPointF &pt : points)
                p.drawEllipse(pt, 3.5, 3.5);
        }
    }
}

void BarChart::mouseMoveEvent(QMouseEvent *event)
{
    m_lastPos = event->pos();
    int hit = -1;
    for (const Rect &r : m_rects) {
        if (r.bar.adjusted(-4, -4, 4, 4).contains(event->pos())) {
            hit = r.index;
            break;
        }
    }
    if (hit != m_hoverIndex) {
        m_hoverIndex = hit;
        update();
    }
    if (hit >= 0 && hit < m_totals.size()) {
        const int total = m_totals.at(hit);
        const int ok = hit < m_ok.size() ? m_ok.at(hit) : 0;
        const int fail = qMax(0, total - ok);
        QToolTip::showText(event->globalPosition().toPoint(),
                           QStringLiteral("%1\nThành công: %2\nThất bại: %3")
                               .arg(hit < m_labels.size() ? m_labels.at(hit) : QString())
                               .arg(ok)
                               .arg(fail),
                           this);
    } else {
        QToolTip::hideText();
    }
}

void BarChart::leaveEvent(QEvent *)
{
    m_hoverIndex = -1;
    QToolTip::hideText();
    update();
}