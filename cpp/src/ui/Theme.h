#pragma once

#include <QString>

namespace Theme
{
inline QString qss()
{
    return QStringLiteral(R"(
/* ===== Google Material / Workspace ===== */
QMainWindow, QWidget {
    background-color: #ffffff;
    color: #202124;
    font-family: "Google Sans", "Roboto", "Noto Sans", "Segoe UI", Arial, sans-serif;
    font-size: 14px;
}
QWidget:focus { outline: none; }

/* ===== Canvas nền dashboard (xám nhạt, card trắng nổi lên) ===== */
QWidget#pagePanel {
    background-color: #f8f9fa;
}

/* ===== Khung tiêu đề ===== */
QFrame#headerFrame {
    background-color: #ffffff;
    border: 1px solid #e8eaed;
    border-radius: 16px;
}
QLabel#appLogo {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                stop:0 #4285F4, stop:0.5 #34A853,
                                stop:0.75 #FBBC04, stop:1 #EA4335);
    color: #ffffff;
    border-radius: 20px;
    font-size: 20px;
    font-weight: 700;
}
QLabel#appTitle {
    font-size: 20px;
    font-weight: 500;
    color: #202124;
}
QLabel#appSubtitle {
    font-size: 12px;
    color: #5f6368;
}
QLabel#appAuthor {
    font-size: 12px;
    font-weight: 600;
    color: #1a73e8;
    background-color: #e8f0fe;
    border: none;
    border-radius: 18px;
    padding: 6px 16px;
}

/* ===== Sidebar trắng (light theme) ===== */
QFrame#sidebar {
    background: #ffffff;
    border: 1px solid #e8eaed;
    border-radius: 16px;
}
QLabel#sideBrand {
    font-size: 16px;
    font-weight: 700;
    color: #202124;
    padding: 8px 14px 4px 14px;
}
QLabel#sideVersion {
    font-size: 11px;
    color: #9aa0a6;
    padding: 4px 14px 6px 14px;
}
QListWidget#navList {
    background: transparent;
    border: none;
    font-size: 14px;
    font-weight: 500;
    color: #3c4043;
}
QListWidget#navList::item {
    border-radius: 10px;
    padding: 10px 14px;
    margin: 2px 6px;
    color: #3c4043;
}
QListWidget#navList::item:hover {
    background: #f1f3f4;
    color: #202124;
}
QListWidget#navList::item:selected {
    background: #e8f0fe;
    color: #1a73e8;
    font-weight: 600;
}
QListWidget#navList::item:selected:hover {
    background: #d2e3fc;
    color: #1a73e8;
}

/* ===== Hero banner (trắng sáng) ===== */
QFrame#heroBanner {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                stop:0 #ffffff, stop:1 #f8faff);
    border: 1px solid #e0e6f0;
    border-radius: 18px;
}
QLabel#heroKicker {
    color: #1a73e8;
    font-size: 11px;
    font-weight: 600;
    letter-spacing: 2px;
    background: transparent;
}
QLabel#heroValue {
    color: #202124;
    font-size: 40px;
    font-weight: 700;
    background: transparent;
}
QLabel#heroCaption {
    color: #5f6368;
    font-size: 12px;
    background: transparent;
}
QLabel#heroRate {
    color: #5f6368;
    font-size: 12px;
    font-weight: 500;
    background: transparent;
}
QProgressBar#heroProgress {
    border: none;
    background-color: #e8eaed;
    border-radius: 4px;
    min-height: 8px;
    max-height: 8px;
    text-align: center;
    color: transparent;
}
QProgressBar#heroProgress::chunk {
    background-color: #1a73e8;
    border-radius: 4px;
}
QPushButton#heroButton {
    background: #1a73e8;
    color: #ffffff;
    border: none;
    border-radius: 22px;
    padding: 10px 22px;
    font-weight: 600;
}
QPushButton#heroButton:hover { background: #1765cc; }
QPushButton#heroButton:pressed { background: #185abc; }

/* ===== Tiêu đề trang (Google Workspace) ===== */
QLabel#pageTitle {
    font-size: 22px;
    font-weight: 600;
    color: #202124;
}
QLabel#pageSubtitle {
    font-size: 13px;
    color: #5f6368;
}

/* ===== KPI cards ===== */
QFrame#statCard {
    background: #ffffff;
    border: 1px solid #e8eaed;
    border-radius: 14px;
}
QFrame#statCard:hover {
    border: 1px solid #c6dafc;
    background: #fdfeff;
}
QLabel#cardLabel {
    color: #5f6368;
    font-size: 12px;
    font-weight: 500;
}
QLabel#cardCaption {
    color: #9aa0a6;
    font-size: 11px;
}
QLabel#cardValue, QLabel#cardValueGreen, QLabel#cardValueRed {
    font-size: 30px;
    font-weight: 600;
    border: none;
    background: transparent;
}
QLabel#cardValue { color: #202124; }
QLabel#cardValueGreen { color: #188038; }
QLabel#cardValueRed { color: #d93025; }

/* ===== Chip trực tuyến (header dashboard) ===== */
QLabel#chipOnline {
    background: #e6f4ea;
    color: #188038;
    border: none;
    border-radius: 16px;
    padding: 5px 14px;
    font-weight: 600;
    font-size: 12px;
}

/* ===== Panel card (chart, bảng) ===== */
QGroupBox#panelCard {
    background: #ffffff;
    border: 1px solid #e8eaed;
    border-radius: 14px;
    margin-top: 18px;
    padding-top: 10px;
}
QGroupBox#panelCard::title {
    subcontrol-origin: margin;
    left: 16px;
    padding: 0 6px;
    color: #202124;
    font-weight: 600;
    font-size: 13px;
}
QLabel#legendOk, QLabel#legendFail {
    font-size: 11px;
    font-weight: 500;
    padding: 2px 10px;
    border-radius: 12px;
}
QLabel#legendOk { color: #188038; background: #e6f4ea; }
QLabel#legendFail { color: #d93025; background: #fce8e6; }

/* ===== Bảng dữ liệu ===== */
QTableWidget#dataTable {
    background: #ffffff;
    border: none;
    gridline-color: #f1f3f4;
    alternate-background-color: #f8f9fa;
    selection-background-color: #e8f0fe;
    selection-color: #202124;
}
QTableWidget#dataTable::item:hover {
    background: #f1f3f4;
}
QTableWidget#dataTable::item:selected {
    background: #e8f0fe;
}
QTableWidget#dataTable QHeaderView::section {
    background: #f8f9fa;
    border: none;
    border-bottom: 1px solid #e8eaed;
    padding: 10px 6px;
    font-weight: 600;
    color: #5f6368;
}
QListWidget#recentList {
    background: #ffffff;
    border: none;
    padding: 2px;
    color: #3c4043;
    font-size: 12px;
}
QListWidget#recentList::item {
    padding: 6px 8px;
    border-radius: 8px;
}
QListWidget#recentList::item:hover {
    background: #f1f3f4;
}

/* ===== Nút bấm (Material filled/outlined) ===== */
QPushButton {
    background-color: #f1f3f4;
    color: #3c4043;
    border: none;
    border-radius: 20px;
    padding: 8px 20px;
    font-weight: 500;
}
QPushButton:hover {
    background-color: #e8eaed;
}
QPushButton:pressed {
    background-color: #dadce0;
}
QPushButton:disabled {
    color: #bdc1c6;
    background-color: #f1f3f4;
}
QPushButton#primaryButton {
    background-color: #1a73e8;
    color: #ffffff;
}
QPushButton#primaryButton:hover { background-color: #1765cc; }
QPushButton#primaryButton:pressed { background-color: #185abc; }
QPushButton#primaryButton:disabled {
    background-color: #f1f3f4;
    color: #bdc1c6;
}
QPushButton#outlinedButton {
    background-color: #ffffff;
    color: #1a73e8;
    border: 1px solid #dadce0;
}
QPushButton#outlinedButton:hover { background-color: #f8faff; border-color: #c6dafc; }
QPushButton#outlinedButton:pressed { background-color: #e8f0fe; }
QPushButton#dangerButton {
    color: #d93025;
    background-color: #ffffff;
    border: 1px solid #dadce0;
}
QPushButton#dangerButton:hover { background-color: #fce8e6; border-color: #fce8e6; }
QPushButton#dangerButton:pressed { background-color: #f5d5d2; }
QPushButton#successButton {
    background-color: #188038;
    color: #ffffff;
}
QPushButton#successButton:hover { background-color: #146c2e; }
QPushButton#successButton:pressed { background-color: #0f5c27; }
QPushButton#successButton:disabled {
    background-color: #f1f3f4;
    color: #bdc1c6;
}

/* ===== Ô nhập liệu (filled, kiểu Google) ===== */
QLineEdit, QTextEdit, QPlainTextEdit, QSpinBox, QComboBox, QTimeEdit {
    background: #f8f9fa;
    border: 1px solid transparent;
    border-bottom: 2px solid #dadce0;
    border-radius: 8px;
    padding: 8px 12px;
    color: #202124;
    selection-background-color: #c6dafc;
}
QLineEdit:hover, QTextEdit:hover, QPlainTextEdit:hover, QSpinBox:hover, QComboBox:hover, QTimeEdit:hover {
    background: #f1f3f4;
}
QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QSpinBox:focus, QComboBox:focus, QTimeEdit:focus {
    border-bottom: 2px solid #1a73e8;
    background: #ffffff;
}
QSpinBox::up-button, QSpinBox::down-button {
    width: 18px;
    border: none;
    background: transparent;
}
QComboBox::drop-down {
    border: none;
    width: 24px;
}
QComboBox QAbstractItemView {
    border: 1px solid #dadce0;
    background: #ffffff;
    color: #202124;
    selection-background-color: #e8f0fe;
    selection-color: #202124;
    outline: none;
}

/* ===== Checkbox (giữ indicator gốc để có dấu tích rõ) ===== */
QCheckBox {
    color: #3c4043;
    spacing: 8px;
}

/* ===== Hộp nhóm (card) ===== */
QGroupBox {
    background: #ffffff;
    border: 1px solid #e8eaed;
    border-radius: 16px;
    margin-top: 26px;
    padding-top: 14px;
    font-weight: 600;
    color: #1a73e8;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 16px;
    padding: 2px 10px;
    color: #1a73e8;
    background: #ffffff;
    border-radius: 8px;
}

/* ===== Bảng ===== */
QTableWidget {
    background: #ffffff;
    border: 1px solid #e8eaed;
    border-radius: 16px;
    gridline-color: #f1f3f4;
    alternate-background-color: #f8f9fa;
    selection-background-color: #e8f0fe;
    selection-color: #202124;
}
QHeaderView::section {
    background: #f8f9fa;
    border: none;
    border-bottom: 1px solid #e8eaed;
    padding: 10px 6px;
    font-weight: 600;
    color: #5f6368;
}

/* ===== Nhật ký ===== */
QTextEdit#logView {
    font-family: "Roboto Mono", "DejaVu Sans Mono", "Noto Sans Mono", monospace;
    font-size: 11px;
    background-color: #f8f9fa;
    color: #3c4043;
    border: 1px solid #e8eaed;
    border-radius: 12px;
    padding: 8px;
}
QLabel#logTitle {
    color: #5f6368;
    font-weight: 600;
    font-size: 12px;
    padding-left: 4px;
}

/* ===== Chip trạng thái ===== */
QListWidget {
    background: #ffffff;
    border: 1px solid #e8eaed;
    border-radius: 12px;
    padding: 4px;
    color: #3c4043;
}
QListWidget::item {
    padding: 4px 8px;
    border-radius: 6px;
}
QListWidget::item:hover {
    background: #f1f3f4;
}

QLabel#chip {
    background-color: #e8f0fe;
    color: #1a73e8;
    border: none;
    border-radius: 16px;
    padding: 4px 14px;
    font-weight: 600;
}
QLabel#chipGreen {
    background-color: #e6f4ea;
    color: #188038;
    border: none;
    border-radius: 16px;
    padding: 4px 14px;
    font-weight: 600;
}
QLabel#chipRed {
    background-color: #fce8e6;
    color: #d93025;
    border: none;
    border-radius: 16px;
    padding: 4px 14px;
    font-weight: 600;
}

/* ===== Thanh tiến độ ===== */
QProgressBar {
    border: none;
    background-color: #e8eaed;
    border-radius: 3px;
    min-height: 8px;
    max-height: 8px;
    text-align: center;
    color: transparent;
}
QProgressBar::chunk {
    background-color: #1a73e8;
    border-radius: 3px;
}

/* ===== Thanh cuộn ===== */
QScrollBar:vertical {
    background: transparent;
    width: 10px;
    margin: 0;
}
QScrollBar::handle:vertical {
    background: #dadce0;
    border-radius: 5px;
    min-height: 30px;
}
QScrollBar::handle:vertical:hover { background: #9aa0a6; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar:horizontal {
    background: transparent;
    height: 10px;
    margin: 0;
}
QScrollBar::handle:horizontal {
    background: #dadce0;
    border-radius: 5px;
    min-width: 30px;
}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }

/* ===== QMessageBox / QDialog ===== */
QMessageBox, QDialog {
    background-color: #ffffff;
}
QMessageBox QLabel {
    color: #202124;
}
QDialogButtonBox QPushButton {
    min-width: 90px;
}
QToolTip {
    background: #202124;
    color: #ffffff;
    border: none;
    border-radius: 4px;
    padding: 6px 8px;
}
)");
}
} // namespace Theme
