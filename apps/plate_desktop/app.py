from __future__ import annotations

import argparse
import base64
import csv
import json
import os
import sqlite3
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any

from PySide6.QtCore import QDateTime, QProcess, QSettings, QSize, Qt, QTimer
from PySide6.QtGui import QAction, QColor, QFont, QImage, QPainter, QPen, QPixmap
from PySide6.QtWidgets import (
    QApplication,
    QComboBox,
    QDialog,
    QDialogButtonBox,
    QFileDialog,
    QFormLayout,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QSplitter,
    QStyle,
    QTabWidget,
    QTableWidget,
    QTableWidgetItem,
    QToolBar,
    QVBoxLayout,
    QWidget,
)


ROOT = Path(__file__).resolve().parents[2]
CLI_CANDIDATES = [
    ROOT / "build" / "release" / "bin" / "Release" / "plate_cli.exe",
    ROOT / "build" / "debug" / "bin" / "Debug" / "plate_cli.exe",
]
HYPERLPR_DETECTOR = (
    ROOT
    / "third_party"
    / "hyperlpr"
    / "models"
    / "20230229"
    / "onnx"
    / "y5fu_640x_sim.onnx"
)
HYPERLPR_RECOGNIZER = (
    ROOT
    / "third_party"
    / "hyperlpr"
    / "models"
    / "20230229"
    / "onnx"
    / "rpv3_mdict_160_r3.onnx"
)


@dataclass
class AppSettings:
    source: str
    engine: str
    detector_model: str
    recognizer_model: str
    alphabet: str
    hyperlpr_tokens: str
    database: str
    snapshots: str
    mock_plate: str
    fps: int


def default_settings() -> AppSettings:
    engine = "hyperlpr" if HYPERLPR_DETECTOR.exists() and HYPERLPR_RECOGNIZER.exists() else "mock"
    return AppSettings(
        source="",
        engine=engine,
        detector_model=str(HYPERLPR_DETECTOR),
        recognizer_model=str(HYPERLPR_RECOGNIZER),
        alphabet=str(ROOT / "configs" / "plate_alphabet.txt"),
        hyperlpr_tokens=str(ROOT / "configs" / "hyperlpr_tokens.txt"),
        database=str(ROOT / "data" / "plate_vision.db"),
        snapshots=str(ROOT / "data" / "snapshots"),
        mock_plate="京A12345",
        fps=25,
    )


def load_settings() -> AppSettings:
    values = default_settings()
    settings = QSettings("PlateVision", "Desktop")
    for field in values.__dataclass_fields__:
        raw = settings.value(field)
        if raw is not None:
            current = getattr(values, field)
            setattr(values, field, int(raw) if isinstance(current, int) else str(raw))
    return values


def save_settings(values: AppSettings) -> None:
    settings = QSettings("PlateVision", "Desktop")
    for field in values.__dataclass_fields__:
        settings.setValue(field, getattr(values, field))


def find_cli() -> Path | None:
    configured = os.environ.get("PLATEVISION_CLI")
    candidates = ([Path(configured)] if configured else []) + CLI_CANDIDATES
    return next((path for path in candidates if path.exists()), None)


def format_timestamp(timestamp_us: int) -> str:
    return QDateTime.fromSecsSinceEpoch(timestamp_us // 1_000_000).toString(
        "yyyy-MM-dd HH:mm:ss"
    )


def decode_jpeg_base64(value: str) -> QImage:
    image = QImage()
    if value:
        image.loadFromData(base64.b64decode(value), "JPG")
    return image


class SettingsDialog(QDialog):
    def __init__(self, values: AppSettings, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setWindowTitle("模型与存储设置")
        self.resize(760, 420)
        self.values = values

        layout = QFormLayout(self)
        self.engine = QComboBox()
        self.engine.addItem("HyperLPR3", "hyperlpr")
        self.engine.addItem("通用 ONNX", "onnx")
        self.engine.addItem("模拟模型", "mock")
        index = self.engine.findData(values.engine)
        self.engine.setCurrentIndex(max(0, index))

        self.detector = self._path_editor(values.detector_model)
        self.recognizer = self._path_editor(values.recognizer_model)
        self.alphabet = self._path_editor(values.alphabet)
        self.tokens = self._path_editor(values.hyperlpr_tokens)
        self.database = self._path_editor(values.database)
        self.snapshots = self._path_editor(values.snapshots)
        self.mock_plate = QLineEdit(values.mock_plate)

        layout.addRow("推理引擎", self.engine)
        layout.addRow("检测模型", self.detector[0])
        layout.addRow("识别模型", self.recognizer[0])
        layout.addRow("通用字符表", self.alphabet[0])
        layout.addRow("HyperLPR 字符表", self.tokens[0])
        layout.addRow("SQLite 数据库", self.database[0])
        layout.addRow("截图目录", self.snapshots[0])
        layout.addRow("模拟车牌", self.mock_plate)

        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Save
            | QDialogButtonBox.StandardButton.Cancel
        )
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        layout.addRow(buttons)

    def _path_editor(self, value: str) -> tuple[QWidget, QLineEdit]:
        container = QWidget()
        row = QHBoxLayout(container)
        row.setContentsMargins(0, 0, 0, 0)
        editor = QLineEdit(value)
        button = QPushButton("浏览")
        button.clicked.connect(lambda: self._browse(editor))
        row.addWidget(editor, 1)
        row.addWidget(button)
        return container, editor

    def _browse(self, editor: QLineEdit) -> None:
        path, _ = QFileDialog.getOpenFileName(self, "选择文件", editor.text())
        if path:
            editor.setText(path)

    def result_settings(self) -> AppSettings:
        return AppSettings(
            source=self.values.source,
            engine=str(self.engine.currentData()),
            detector_model=self.detector[1].text().strip(),
            recognizer_model=self.recognizer[1].text().strip(),
            alphabet=self.alphabet[1].text().strip(),
            hyperlpr_tokens=self.tokens[1].text().strip(),
            database=self.database[1].text().strip(),
            snapshots=self.snapshots[1].text().strip(),
            mock_plate=self.mock_plate.text().strip(),
            fps=self.values.fps,
        )


class MainWindow(QMainWindow):
    def __init__(self, smoke_test: bool = False) -> None:
        super().__init__()
        self.smoke_test = smoke_test
        self.smoke_event_seen = False
        self.smoke_screenshot = ROOT / "output" / "desktop_smoke.png"
        self.settings = load_settings()
        self.process: QProcess | None = None
        self.stdout_buffer = b""
        self.stderr_text = ""
        self.preview_image = QImage()
        self.current_frame_size = QSize()

        self.setWindowTitle("Plate Vision")
        self.resize(1440, 880)
        self._build_ui()
        self._apply_style()
        self.refresh_history()

        if self.smoke_test:
            self.smoke_timeout = QTimer(self)
            self.smoke_timeout.setSingleShot(True)
            self.smoke_timeout.timeout.connect(self._finish_smoke_test)
            self.smoke_timeout.start(20_000)

    def _build_ui(self) -> None:
        toolbar = QToolBar("主工具栏")
        toolbar.setMovable(False)
        toolbar.setIconSize(QSize(18, 18))
        self.addToolBar(toolbar)

        self.open_action = QAction(
            self.style().standardIcon(QStyle.StandardPixmap.SP_DirOpenIcon),
            "打开视频",
            self,
        )
        self.open_action.triggered.connect(self.open_source)
        self.start_action = QAction(
            self.style().standardIcon(QStyle.StandardPixmap.SP_MediaPlay),
            "开始",
            self,
        )
        self.start_action.triggered.connect(self.start_session)
        self.stop_action = QAction(
            self.style().standardIcon(QStyle.StandardPixmap.SP_MediaStop),
            "停止",
            self,
        )
        self.stop_action.triggered.connect(self.stop_session)
        self.stop_action.setEnabled(False)
        self.settings_action = QAction(
            self.style().standardIcon(QStyle.StandardPixmap.SP_FileDialogDetailedView),
            "设置",
            self,
        )
        self.settings_action.triggered.connect(self.open_settings)

        toolbar.addAction(self.open_action)
        toolbar.addAction(self.start_action)
        toolbar.addAction(self.stop_action)
        toolbar.addSeparator()
        toolbar.addWidget(QLabel(" 引擎 "))
        self.engine_combo = QComboBox()
        self.engine_combo.addItem("HyperLPR3", "hyperlpr")
        self.engine_combo.addItem("通用 ONNX", "onnx")
        self.engine_combo.addItem("模拟模型", "mock")
        self.engine_combo.setCurrentIndex(
            max(0, self.engine_combo.findData(self.settings.engine))
        )
        toolbar.addWidget(self.engine_combo)
        toolbar.addSeparator()
        toolbar.addAction(self.settings_action)

        self.source_edit = QLineEdit()
        self.source_edit.setPlaceholderText("视频文件或 rtsp:// 地址")
        self.source_edit.setText(self.settings.source)
        self.source_edit.setMinimumWidth(420)
        toolbar.addWidget(self.source_edit)

        splitter = QSplitter(Qt.Orientation.Horizontal)
        self.video_label = QLabel("等待视频源")
        self.video_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.video_label.setMinimumSize(640, 360)
        self.video_label.setObjectName("videoSurface")
        splitter.addWidget(self.video_label)

        tabs = QTabWidget()
        live_tab = QWidget()
        live_layout = QVBoxLayout(live_tab)
        live_layout.setContentsMargins(8, 8, 8, 8)
        self.live_table = self._create_table(
            ["时间", "车牌", "颜色", "置信度"]
        )
        self.snapshot_label = QLabel("暂无截图")
        self.snapshot_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.snapshot_label.setMinimumHeight(180)
        self.snapshot_label.setObjectName("snapshotSurface")
        live_layout.addWidget(self.live_table, 3)
        live_layout.addWidget(self.snapshot_label, 2)
        tabs.addTab(live_tab, "实时事件")

        history_tab = QWidget()
        history_layout = QVBoxLayout(history_tab)
        history_layout.setContentsMargins(8, 8, 8, 8)
        history_buttons = QHBoxLayout()
        refresh_button = QPushButton("刷新")
        refresh_button.clicked.connect(self.refresh_history)
        export_button = QPushButton("导出 CSV")
        export_button.clicked.connect(self.export_history)
        history_buttons.addWidget(refresh_button)
        history_buttons.addWidget(export_button)
        history_buttons.addStretch(1)
        self.history_table = self._create_table(
            ["时间", "车牌", "颜色", "截图"]
        )
        self.history_table.itemSelectionChanged.connect(
            self.show_history_snapshot
        )
        history_layout.addLayout(history_buttons)
        history_layout.addWidget(self.history_table, 1)
        tabs.addTab(history_tab, "历史记录")
        tabs.currentChanged.connect(lambda _: self.refresh_history())
        self.tabs = tabs

        splitter.addWidget(tabs)
        splitter.setSizes([940, 500])
        self.setCentralWidget(splitter)

        self.status_label = QLabel("就绪")
        self.model_label = QLabel("模型未启动")
        self.statusBar().addWidget(self.status_label, 1)
        self.statusBar().addPermanentWidget(self.model_label)

    def _create_table(self, headers: list[str]) -> QTableWidget:
        table = QTableWidget(0, len(headers))
        table.setHorizontalHeaderLabels(headers)
        table.setSelectionBehavior(
            QTableWidget.SelectionBehavior.SelectRows
        )
        table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        table.verticalHeader().setVisible(False)
        table.horizontalHeader().setSectionResizeMode(
            QHeaderView.ResizeMode.ResizeToContents
        )
        table.horizontalHeader().setStretchLastSection(True)
        return table

    def _apply_style(self) -> None:
        self.setStyleSheet(
            """
            QMainWindow { background: #f3f5f7; }
            QToolBar {
                background: #ffffff;
                border-bottom: 1px solid #d8dde3;
                spacing: 6px;
                padding: 5px;
            }
            QLineEdit, QComboBox {
                background: #ffffff;
                border: 1px solid #c9d0d8;
                border-radius: 4px;
                padding: 4px 7px;
                min-height: 24px;
            }
            QPushButton {
                background: #ffffff;
                border: 1px solid #c9d0d8;
                border-radius: 4px;
                padding: 5px 10px;
            }
            QPushButton:hover { background: #eef2f6; }
            QTabWidget::pane {
                border: 1px solid #d8dde3;
                background: #ffffff;
            }
            QTabBar::tab {
                padding: 7px 14px;
                background: #e9edf1;
                border: 1px solid #d8dde3;
                border-bottom: none;
            }
            QTabBar::tab:selected { background: #ffffff; }
            QTableWidget {
                background: #ffffff;
                alternate-background-color: #f7f9fb;
                border: none;
                gridline-color: #edf0f3;
            }
            QHeaderView::section {
                background: #f0f3f6;
                border: none;
                border-bottom: 1px solid #d8dde3;
                padding: 6px;
                font-weight: 500;
            }
            QLabel#videoSurface {
                background: #1e252d;
                color: #aeb8c2;
                border: 1px solid #c9d0d8;
            }
            QLabel#snapshotSurface {
                background: #232b34;
                color: #aeb8c2;
                border: 1px solid #d8dde3;
            }
            QStatusBar { background: #ffffff; }
            """
        )

    def open_source(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self,
            "选择视频",
            str(ROOT / "output"),
            "Video (*.mp4 *.avi *.mov *.mkv);;Images (*.jpg *.jpeg *.png);;All (*)",
        )
        if path:
            self.source_edit.setText(path)

    def open_settings(self) -> None:
        dialog = SettingsDialog(self.settings, self)
        if dialog.exec() == QDialog.DialogCode.Accepted:
            self.settings = dialog.result_settings()
            self.engine_combo.setCurrentIndex(
                max(0, self.engine_combo.findData(self.settings.engine))
            )
            save_settings(self.settings)
            self.model_label.setText("设置已保存")
            self.refresh_history()

    def _cli_path(self) -> Path | None:
        return find_cli()

    def start_session(self) -> None:
        if self.process and self.process.state() != QProcess.ProcessState.NotRunning:
            return
        source = self.source_edit.text().strip()
        if not source:
            QMessageBox.warning(self, "缺少视频源", "请选择视频文件或输入视频流地址。")
            return
        cli = self._cli_path()
        if cli is None:
            QMessageBox.critical(self, "缺少后端", "未找到 plate_cli.exe，请先完成 Release 构建。")
            return

        self.settings.source = source
        self.settings.engine = str(self.engine_combo.currentData())
        save_settings(self.settings)
        self.live_table.setRowCount(0)
        self.snapshot_label.setText("暂无截图")
        self.snapshot_label.setPixmap(QPixmap())
        self.stdout_buffer = b""
        self.stderr_text = ""

        arguments = [
            "--input",
            source,
            "--engine",
            self.settings.engine,
            "--ui-stream",
            "--control-stdin",
            "--database",
            self.settings.database,
            "--snapshot-dir",
            self.settings.snapshots,
            "--queue-capacity",
            "4" if source.lower().startswith("rtsp") else "128",
            "--fps",
            str(self.settings.fps),
        ]
        if self.settings.engine == "hyperlpr":
            arguments += [
                "--detector-model",
                self.settings.detector_model,
                "--recognizer-model",
                self.settings.recognizer_model,
                "--hyperlpr-tokens",
                self.settings.hyperlpr_tokens,
                "--score-threshold",
                "0.30",
                "--iou-threshold",
                "0.60",
            ]
        elif self.settings.engine == "onnx":
            arguments += [
                "--detector-model",
                self.settings.detector_model,
                "--recognizer-model",
                self.settings.recognizer_model,
                "--alphabet",
                self.settings.alphabet,
            ]
        else:
            arguments += [
                "--plate",
                self.settings.mock_plate,
                "--color",
                "blue",
            ]

        self.process = QProcess(self)
        self.process.setWorkingDirectory(str(ROOT))
        self.process.setProcessChannelMode(QProcess.ProcessChannelMode.SeparateChannels)
        self.process.readyReadStandardOutput.connect(self.read_stdout)
        self.process.readyReadStandardError.connect(self.read_stderr)
        self.process.finished.connect(self.process_finished)
        self.process.errorOccurred.connect(self.process_error)
        self.process.start(str(cli), arguments)

        self.start_action.setEnabled(False)
        self.stop_action.setEnabled(True)
        self.open_action.setEnabled(False)
        self.settings_action.setEnabled(False)
        self.engine_combo.setEnabled(False)
        self.status_label.setText("正在启动模型")
        self.model_label.setText(self._engine_text())

    def stop_session(self) -> None:
        if self.process and self.process.state() != QProcess.ProcessState.NotRunning:
            self.status_label.setText("正在停止")
            self.process.write(b"stop\n")
            if not self.process.waitForFinished(2000):
                self.process.terminate()
            if self.process.state() != QProcess.ProcessState.NotRunning and not self.process.waitForFinished(1000):
                self.process.kill()
                self.process.waitForFinished(1000)

    def read_stdout(self) -> None:
        if not self.process:
            return
        self.stdout_buffer += bytes(self.process.readAllStandardOutput())
        while b"\n" in self.stdout_buffer:
            line, self.stdout_buffer = self.stdout_buffer.split(b"\n", 1)
            if not line.strip():
                continue
            try:
                message = json.loads(line.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError):
                continue
            self.handle_message(message)

    def read_stderr(self) -> None:
        if self.process:
            data = bytes(self.process.readAllStandardError())
            self.stderr_text = (self.stderr_text + data.decode("utf-8", "replace"))[-6000:]

    def handle_message(self, message: dict[str, Any]) -> None:
        message_type = message.get("type")
        if message_type == "status":
            self.status_label.setText(str(message.get("message", "运行中")))
        elif message_type == "error":
            self.status_label.setText(f"错误：{message.get('message', '')}")
        elif message_type == "frame":
            self.update_preview(message)
            for event in message.get("events", []):
                self.add_live_event(event)
        elif message_type == "finished":
            self.status_label.setText(
                f"完成，处理 {message.get('processed_frames', 0)} 帧，"
                f"事件 {message.get('recognized_events', 0)} 条"
            )
            self.refresh_history()

    def update_preview(self, message: dict[str, Any]) -> None:
        image = decode_jpeg_base64(str(message.get("image_jpeg", "")))
        if image.isNull():
            return
        frame_width = int(message.get("frame_width", image.width()))
        frame_height = int(message.get("frame_height", image.height()))
        painter = QPainter(image)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        painter.setFont(QFont("Microsoft YaHei", 11))
        scale_x = image.width() / max(1, frame_width)
        scale_y = image.height() / max(1, frame_height)
        for observation in message.get("observations", []):
            box = observation.get("box", {})
            rect = (
                int(box.get("x", 0) * scale_x),
                int(box.get("y", 0) * scale_y),
                int(box.get("width", 0) * scale_x),
                int(box.get("height", 0) * scale_y),
            )
            color = QColor("#16a34a") if observation.get("valid") else QColor("#dc2626")
            painter.setPen(QPen(color, 3))
            painter.drawRect(*rect)
            label = str(observation.get("text", ""))
            if label:
                metrics = painter.fontMetrics()
                text_width = metrics.horizontalAdvance(label) + 10
                text_height = metrics.height() + 4
                label_x = min(rect[0], image.width() - text_width)
                label_y = max(0, rect[1] - text_height)
                painter.fillRect(label_x, label_y, text_width, text_height, color)
                painter.setPen(QColor("#ffffff"))
                painter.drawText(label_x + 5, label_y + metrics.ascent() + 2, label)
        painter.end()
        self.preview_image = image
        self.current_frame_size = QSize(frame_width, frame_height)
        self._render_preview()

    def _render_preview(self) -> None:
        if self.preview_image.isNull():
            return
        pixmap = QPixmap.fromImage(self.preview_image).scaled(
            self.video_label.size(),
            Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.SmoothTransformation,
        )
        self.video_label.setPixmap(pixmap)

    def add_live_event(self, event: dict[str, Any]) -> None:
        self.live_table.insertRow(0)
        timestamp_us = int(event.get("captured_at_us", 0))
        values = [
            format_timestamp(timestamp_us),
            str(event.get("plate_number", "")),
            str(event.get("color", "")),
            f"{float(event.get('confidence', 0.0)):.3f}",
        ]
        for column, value in enumerate(values):
            self.live_table.setItem(0, column, QTableWidgetItem(value))
        image = decode_jpeg_base64(str(event.get("snapshot_jpeg", "")))
        if not image.isNull():
            self.snapshot_label.setPixmap(
                QPixmap.fromImage(image).scaled(
                    self.snapshot_label.size() - QSize(12, 12),
                    Qt.AspectRatioMode.KeepAspectRatio,
                    Qt.TransformationMode.SmoothTransformation,
                )
            )
        if self.smoke_test and not self.smoke_event_seen:
            self.smoke_event_seen = True
            QTimer.singleShot(350, self._finish_smoke_test)

    def process_finished(self) -> None:
        self.start_action.setEnabled(True)
        self.stop_action.setEnabled(False)
        self.open_action.setEnabled(True)
        self.settings_action.setEnabled(True)
        self.engine_combo.setEnabled(True)
        if "complete" not in self.status_label.text().lower():
            self.status_label.setText("处理结束")
        if self.stderr_text.strip():
            self.model_label.setToolTip(self.stderr_text[-2000:])
        self.refresh_history()

    def process_error(self, error: QProcess.ProcessError) -> None:
        if error != QProcess.ProcessError.Crashed:
            self.status_label.setText(f"后端启动失败：{error}")

    def _engine_text(self) -> str:
        labels = {
            "hyperlpr": "HyperLPR3 / CPU",
            "onnx": "ONNX / CPU",
            "mock": "Mock / CPU",
        }
        return labels.get(self.settings.engine, self.settings.engine)

    def refresh_history(self) -> None:
        self.history_table.setRowCount(0)
        database = Path(self.settings.database)
        if not database.exists():
            return
        try:
            connection = sqlite3.connect(f"file:{database.as_posix()}?mode=ro", uri=True)
            rows = connection.execute(
                "SELECT id, plate_number, plate_color, captured_at_ms, snapshot_path "
                "FROM recognition_events ORDER BY captured_at_ms DESC LIMIT 200"
            ).fetchall()
            connection.close()
        except sqlite3.Error:
            return
        for row in rows:
            record_id, plate, color, captured_ms, snapshot_path = row
            index = self.history_table.rowCount()
            self.history_table.insertRow(index)
            values = [
                QDateTime.fromMSecsSinceEpoch(captured_ms).toString("yyyy-MM-dd HH:mm:ss"),
                str(plate),
                str(color),
                str(snapshot_path or ""),
            ]
            for column, value in enumerate(values):
                item = QTableWidgetItem(value)
                item.setData(Qt.ItemDataRole.UserRole, record_id)
                self.history_table.setItem(index, column, item)

    def show_history_snapshot(self) -> None:
        selected = self.history_table.selectedItems()
        if not selected:
            return
        row = selected[0].row()
        path_item = self.history_table.item(row, 3)
        if path_item is None or not path_item.text():
            return
        path = Path(self.settings.snapshots) / path_item.text()
        image = QImage(str(path))
        if not image.isNull():
            self.snapshot_label.setPixmap(
                QPixmap.fromImage(image).scaled(
                    self.snapshot_label.size() - QSize(12, 12),
                    Qt.AspectRatioMode.KeepAspectRatio,
                    Qt.TransformationMode.SmoothTransformation,
                )
            )

    def export_history(self) -> None:
        path, _ = QFileDialog.getSaveFileName(
            self, "导出记录", str(ROOT / "output" / "records.csv"), "CSV (*.csv)"
        )
        if not path:
            return
        database = Path(self.settings.database)
        if not database.exists():
            QMessageBox.information(self, "没有记录", "数据库尚不存在。")
            return
        connection = sqlite3.connect(f"file:{database.as_posix()}?mode=ro", uri=True)
        rows = connection.execute(
            "SELECT id, camera_id, stream_id, track_id, plate_number, plate_color, "
            "confidence, captured_at_ms, snapshot_path FROM recognition_events "
            "ORDER BY captured_at_ms DESC"
        ).fetchall()
        connection.close()
        with open(path, "w", encoding="utf-8-sig", newline="") as output:
            writer = csv.writer(output)
            writer.writerow(
                [
                    "id",
                    "camera_id",
                    "stream_id",
                    "track_id",
                    "plate_number",
                    "color",
                    "confidence",
                    "captured_at",
                    "snapshot_path",
                ]
            )
            for row in rows:
                writer.writerow(
                    row[:7]
                    + [QDateTime.fromMSecsSinceEpoch(row[7]).toString("yyyy-MM-dd HH:mm:ss")]
                    + [row[8]]
                )
        self.status_label.setText(f"已导出 {len(rows)} 条记录")

    def resizeEvent(self, event) -> None:
        super().resizeEvent(event)
        self._render_preview()

    def _finish_smoke_test(self) -> None:
        self.smoke_screenshot.parent.mkdir(parents=True, exist_ok=True)
        self.grab().save(str(self.smoke_screenshot), "PNG")
        if self.process and self.process.state() != QProcess.ProcessState.NotRunning:
            self.stop_session()
        QApplication.instance().quit()

    def closeEvent(self, event) -> None:
        self.stop_session()
        super().closeEvent(event)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument("--smoke-test", action="store_true")
    parser.add_argument("--source")
    parser.add_argument("--engine", choices=["hyperlpr", "onnx", "mock"])
    parser.add_argument("--screenshot")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    application = QApplication(sys.argv[:1])
    application.setApplicationName("Plate Vision")
    window = MainWindow(smoke_test=arguments.smoke_test)
    if arguments.screenshot:
        window.smoke_screenshot = Path(arguments.screenshot)
    if arguments.source:
        window.source_edit.setText(arguments.source)
        window.settings.source = arguments.source
    if arguments.engine:
        index = window.engine_combo.findData(arguments.engine)
        if index >= 0:
            window.engine_combo.setCurrentIndex(index)
            window.settings.engine = arguments.engine
    window.show()
    if arguments.smoke_test:
        QTimer.singleShot(300, window.start_session)
    return application.exec()


if __name__ == "__main__":
    raise SystemExit(main())

