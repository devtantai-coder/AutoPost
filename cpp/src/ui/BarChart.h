#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

// Biểu đồ cột (không phụ thuộc QtCharts): tổng số bài mỗi ngày,
// phần xanh = thành công, phần đỏ = thất bại.
//    - Cột gradient, bo góc
//    - Hover cột -> hiện tooltip chi tiết
//    - Hiển thị số liệu trên đỉnh cột
//    - Cột ngày cuối (hôm nay) được đánh dấu riêng
class BarChart : public QWidget
{
    Q_OBJECT
public:
    explicit BarChart(QWidget *parent = nullptr);

    void setData(const QStringList &labels, const QVector<int> &totals,
                 const QVector<int> &okValues);

    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    struct Rect
    {
        QRect bar;
        int index = -1;
    };

    QStringList m_labels;
    QVector<int> m_totals;
    QVector<int> m_ok;
    QVector<Rect> m_rects;
    int m_hoverIndex = -1;
    QPoint m_lastPos;
};