#pragma once

#include <QWidget>

// Donut chart đơn giản (không phụ thuộc QtCharts): vòng tròn tỷ lệ
// thành công/thất bại với số phần trăm ở giữa.
class DonutChart : public QWidget
{
    Q_OBJECT
public:
    explicit DonutChart(QWidget *parent = nullptr);

    void setData(int ok, int total);

    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int m_ok = 0;
    int m_total = 0;
};