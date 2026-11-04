#include "main_window.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QSplitter>
#include <QSpinBox>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStatusBar>
#include <QStyle>
#include <QStringConverter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextStream>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include <array>
#include <utility>

namespace {

QString rootDirectory() {
    return QString::fromUtf8(PLATEVISION_ROOT);
}

QString defaultDetectorPath() {
    return QDir(rootDirectory()).filePath(
        "third_party/hyperlpr/models/20230229/onnx/y5fu_640x_sim.onnx");
}

QString defaultRecognizerPath() {
    return QDir(rootDirectory()).filePath(
        "third_party/hyperlpr/models/20230229/onnx/rpv3_mdict_160_r3.onnx");
}

QString formatTimestamp(qint64 timestampUs) {
    return QDateTime::fromMSecsSinceEpoch(timestampUs / 1000)
        .toString("yyyy-MM-dd HH:mm:ss");
}

QImage decodeBase64Image(const QString& value) {
    QImage image;
    if (!value.isEmpty()) {
        image.loadFromData(
            QByteArray::fromBase64(value.toLatin1()),
            "JPG");
    }
    return image;
}

QString csvEscape(QString value) {
    if (!value.contains(',') &&
        !value.contains('"') &&
        !value.contains('\n')) {
        return value;
    }
    value.replace("\"", "\"\"");
    return "\"" + value + "\"";
}

class SettingsDialog final : public QDialog {
public:
    SettingsDialog(
        const DesktopSettings& settings,
        QWidget* parent = nullptr)
        : QDialog(parent) {
        setWindowTitle("模型与存储设置");
        resize(760, 430);

        engine_ = new QComboBox(this);
        engine_->addItem("HyperLPR3", "hyperlpr");
        engine_->addItem("通用 ONNX", "onnx");
        engine_->addItem("模拟模型", "mock");
        engine_->setCurrentIndex(
            qMax(0, engine_->findData(settings.engine)));
        provider_ = new QComboBox(this);
        provider_->addItem("CPU", "cpu");
        provider_->addItem("DirectML", "dml");
        provider_->setCurrentIndex(
            qMax(0, provider_->findData(settings.provider)));
        device_ = new QSpinBox(this);
        device_->setRange(0, 16);
        device_->setValue(settings.deviceId);

        detector_ = addPathRow("检测模型", settings.detectorModel);
        recognizer_ = addPathRow("识别模型", settings.recognizerModel);
        classifier_ = addPathRow("颜色分类模型", settings.classifierModel);
        alphabet_ = addPathRow("通用字符表", settings.alphabet);
        tokens_ = addPathRow("HyperLPR 字符表", settings.hyperlprTokens);
        database_ = addPathRow("SQLite 数据库", settings.database);
        snapshots_ = addPathRow("截图目录", settings.snapshots);
        mockPlate_ = new QLineEdit(settings.mockPlate, this);

        auto* form = new QFormLayout(this);
        form->addRow("推理引擎", engine_);
        form->addRow("执行 Provider", provider_);
        form->addRow("DirectML 设备", device_);
        form->addRow("检测模型", detectorRow_);
        form->addRow("识别模型", recognizerRow_);
        form->addRow("颜色分类模型", classifierRow_);
        form->addRow("通用字符表", alphabetRow_);
        form->addRow("HyperLPR 字符表", tokensRow_);
        form->addRow("SQLite 数据库", databaseRow_);
        form->addRow("截图目录", snapshotsRow_);
        form->addRow("模拟车牌", mockPlate_);

        auto* buttons = new QDialogButtonBox(
            QDialogButtonBox::Save | QDialogButtonBox::Cancel,
            this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        form->addRow(buttons);
        original_ = settings;
    }

    DesktopSettings resultSettings() const {
        DesktopSettings result = original_;
        result.engine = engine_->currentData().toString();
        result.provider = provider_->currentData().toString();
        result.deviceId = device_->value();
        result.detectorModel = detector_->text().trimmed();
        result.recognizerModel = recognizer_->text().trimmed();
        result.classifierModel = classifier_->text().trimmed();
        result.alphabet = alphabet_->text().trimmed();
        result.hyperlprTokens = tokens_->text().trimmed();
        result.database = database_->text().trimmed();
        result.snapshots = snapshots_->text().trimmed();
        result.mockPlate = mockPlate_->text().trimmed();
        return result;
    }

private:
    QLineEdit* addPathRow(const QString&, const QString& value) {
        auto* container = new QWidget(this);
        auto* row = new QHBoxLayout(container);
        row->setContentsMargins(0, 0, 0, 0);
        auto* editor = new QLineEdit(value, container);
        auto* browse = new QPushButton("浏览", container);
        connect(browse, &QPushButton::clicked, this, [this, editor] {
            const QString path = QFileDialog::getOpenFileName(
                this,
                "选择文件",
                editor->text());
            if (!path.isEmpty()) {
                editor->setText(path);
            }
        });
        row->addWidget(editor, 1);
        row->addWidget(browse);

        if (detector_ == nullptr && detectorRow_ == nullptr) {
            detectorRow_ = container;
        } else if (recognizer_ == nullptr && recognizerRow_ == nullptr) {
            recognizerRow_ = container;
        } else if (classifier_ == nullptr && classifierRow_ == nullptr) {
            classifierRow_ = container;
        } else if (alphabet_ == nullptr && alphabetRow_ == nullptr) {
            alphabetRow_ = container;
        } else if (tokens_ == nullptr && tokensRow_ == nullptr) {
            tokensRow_ = container;
        } else if (database_ == nullptr && databaseRow_ == nullptr) {
            databaseRow_ = container;
        } else {
            snapshotsRow_ = container;
        }
        return editor;
    }

    DesktopSettings original_;
    QComboBox* engine_{nullptr};
    QComboBox* provider_{nullptr};
    QSpinBox* device_{nullptr};
    QLineEdit* detector_{nullptr};
    QLineEdit* recognizer_{nullptr};
    QLineEdit* classifier_{nullptr};
    QLineEdit* alphabet_{nullptr};
    QLineEdit* tokens_{nullptr};
    QLineEdit* database_{nullptr};
    QLineEdit* snapshots_{nullptr};
    QLineEdit* mockPlate_{nullptr};
    QWidget* detectorRow_{nullptr};
    QWidget* recognizerRow_{nullptr};
    QWidget* classifierRow_{nullptr};
    QWidget* alphabetRow_{nullptr};
    QWidget* tokensRow_{nullptr};
    QWidget* databaseRow_{nullptr};
    QWidget* snapshotsRow_{nullptr};
};

}  // namespace

MainWindow::MainWindow(bool smokeTest, QWidget* parent)
    : QMainWindow(parent),
      smokeTest_(smokeTest),
      smokeScreenshot_(
          QDir(rootDirectory()).filePath("output/desktop_cpp_smoke.png")) {
    settings_ = loadSettings();
    buildUi();
    applyStyle();
    refreshHistory();

    if (smokeTest_) {
        smokeTimer_ = new QTimer(this);
        smokeTimer_->setSingleShot(true);
        connect(smokeTimer_, &QTimer::timeout, this, [this] {
            finishSmokeTest();
        });
        smokeTimer_->start(20000);
    }
}

MainWindow::~MainWindow() {
    stopSession();
}

DesktopSettings MainWindow::defaultSettings() const {
    DesktopSettings settings;
    const QString detector = defaultDetectorPath();
    const QString recognizer = defaultRecognizerPath();
    settings.engine =
        QFileInfo::exists(detector) && QFileInfo::exists(recognizer)
            ? "hyperlpr"
            : "mock";
    settings.provider = "cpu";
    settings.deviceId = 0;
    settings.detectorModel = detector;
    settings.recognizerModel = recognizer;
    settings.classifierModel =
        QDir(rootDirectory()).filePath(
            "third_party/hyperlpr/models/20230229/onnx/"
            "litemodel_cls_96x_r1.onnx");
    settings.alphabet =
        QDir(rootDirectory()).filePath("configs/plate_alphabet.txt");
    settings.hyperlprTokens =
        QDir(rootDirectory()).filePath("configs/hyperlpr_tokens.txt");
    settings.database =
        QDir(rootDirectory()).filePath("data/plate_vision.db");
    settings.snapshots =
        QDir(rootDirectory()).filePath("data/snapshots");
    settings.mockPlate = QString::fromUtf8("京A12345");
    settings.fps = 25;
    return settings;
}

DesktopSettings MainWindow::loadSettings() const {
    DesktopSettings settings = defaultSettings();
    QSettings stored;
    settings.source = stored.value("source", settings.source).toString();
    settings.engine = stored.value("engine", settings.engine).toString();
    settings.provider = stored.value(
        "provider", settings.provider).toString();
    settings.deviceId = stored.value(
        "deviceId", settings.deviceId).toInt();
    settings.detectorModel = stored.value(
        "detectorModel", settings.detectorModel).toString();
    settings.recognizerModel = stored.value(
        "recognizerModel", settings.recognizerModel).toString();
    settings.classifierModel = stored.value(
        "classifierModel", settings.classifierModel).toString();
    settings.alphabet = stored.value(
        "alphabet", settings.alphabet).toString();
    settings.hyperlprTokens = stored.value(
        "hyperlprTokens", settings.hyperlprTokens).toString();
    settings.database = stored.value(
        "database", settings.database).toString();
    settings.snapshots = stored.value(
        "snapshots", settings.snapshots).toString();
    settings.mockPlate = stored.value(
        "mockPlate", settings.mockPlate).toString();
    settings.fps = stored.value("fps", settings.fps).toInt();
    return settings;
}

void MainWindow::saveSettings() {
    QSettings stored;
    stored.setValue("source", settings_.source);
    stored.setValue("engine", settings_.engine);
    stored.setValue("provider", settings_.provider);
    stored.setValue("deviceId", settings_.deviceId);
    stored.setValue("detectorModel", settings_.detectorModel);
    stored.setValue("recognizerModel", settings_.recognizerModel);
    stored.setValue("classifierModel", settings_.classifierModel);
    stored.setValue("alphabet", settings_.alphabet);
    stored.setValue("hyperlprTokens", settings_.hyperlprTokens);
    stored.setValue("database", settings_.database);
    stored.setValue("snapshots", settings_.snapshots);
    stored.setValue("mockPlate", settings_.mockPlate);
    stored.setValue("fps", settings_.fps);
}

QString MainWindow::findCli() const {
    const QString configured = qEnvironmentVariable("PLATEVISION_CLI");
    std::array<QString, 2> candidates = {
        QDir(rootDirectory()).filePath(
            "build/release/bin/Release/plate_cli.exe"),
        QDir(rootDirectory()).filePath(
            "build/debug/bin/Debug/plate_cli.exe")};
    if (!configured.isEmpty()) {
        return QFileInfo::exists(configured) ? configured : QString();
    }
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

QString MainWindow::engineLabel() const {
    if (settings_.engine == "hyperlpr") {
        return QString("HyperLPR3 / %1")
            .arg(settings_.provider == "dml" ? "DirectML" : "CPU");
    }
    if (settings_.engine == "onnx") {
        return QString("ONNX / %1")
            .arg(settings_.provider == "dml" ? "DirectML" : "CPU");
    }
    return "Mock / CPU";
}

void MainWindow::buildUi() {
    setWindowTitle("Plate Vision");
    resize(1440, 880);

    auto* toolbar = addToolBar("主工具栏");
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(18, 18));

    auto* openAction = toolbar->addAction(
        style()->standardIcon(QStyle::SP_DirOpenIcon),
        "打开视频");
    connect(openAction, &QAction::triggered, this, [this] {
        openSource();
    });
    auto* startAction = toolbar->addAction(
        style()->standardIcon(QStyle::SP_MediaPlay),
        "开始");
    connect(startAction, &QAction::triggered, this, [this] {
        startSession();
    });
    auto* stopAction = toolbar->addAction(
        style()->standardIcon(QStyle::SP_MediaStop),
        "停止");
    connect(stopAction, &QAction::triggered, this, [this] {
        stopSession();
    });
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(" 引擎 ", toolbar));
    engineCombo_ = new QComboBox(toolbar);
    engineCombo_->addItem("HyperLPR3", "hyperlpr");
    engineCombo_->addItem("通用 ONNX", "onnx");
    engineCombo_->addItem("模拟模型", "mock");
    engineCombo_->setCurrentIndex(
        qMax(0, engineCombo_->findData(settings_.engine)));
    toolbar->addWidget(engineCombo_);
    toolbar->addWidget(new QLabel(" 倍速 ", toolbar));
    speedCombo_ = new QComboBox(toolbar);
    speedCombo_->addItem("0.5x", 0.5);
    speedCombo_->addItem("1.0x", 1.0);
    speedCombo_->addItem("1.5x", 1.5);
    speedCombo_->addItem("2.0x", 2.0);
    speedCombo_->setCurrentIndex(1);
    speedCombo_->setEnabled(false);
    connect(
        speedCombo_,
        &QComboBox::currentIndexChanged,
        this,
        [this](int) {
            setPlaybackSpeed(speedCombo_->currentData().toDouble());
        });
    toolbar->addWidget(speedCombo_);
    toolbar->addSeparator();
    auto* settingsAction = toolbar->addAction(
        style()->standardIcon(QStyle::SP_FileDialogDetailedView),
        "设置");
    connect(settingsAction, &QAction::triggered, this, [this] {
        openSettings();
    });
    sourceEdit_ = new QLineEdit(settings_.source, toolbar);
    sourceEdit_->setPlaceholderText("视频文件或 rtsp:// 地址");
    sourceEdit_->setMinimumWidth(420);
    toolbar->addWidget(sourceEdit_);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    videoLabel_ = new QLabel("等待视频源", splitter);
    videoLabel_->setAlignment(Qt::AlignCenter);
    videoLabel_->setMinimumSize(640, 360);
    videoLabel_->setObjectName("videoSurface");
    videoLabel_->setCursor(Qt::PointingHandCursor);
    videoLabel_->setToolTip("点击暂停或继续播放");
    videoLabel_->installEventFilter(this);
    splitter->addWidget(videoLabel_);

    tabs_ = new QTabWidget(splitter);
    auto* liveTab = new QWidget(tabs_);
    auto* liveLayout = new QVBoxLayout(liveTab);
    liveLayout->setContentsMargins(8, 8, 8, 8);
    liveTable_ = createTable(
        {"时间", "车牌", "颜色", "置信度"});
    connect(
        liveTable_,
        &QTableWidget::itemSelectionChanged,
        this,
        [this] { showLiveSnapshot(); });
    snapshotLabel_ = new QLabel("暂无截图", liveTab);
    snapshotLabel_->setAlignment(Qt::AlignCenter);
    snapshotLabel_->setMinimumHeight(180);
    snapshotLabel_->setMaximumHeight(230);
    snapshotLabel_->setMargin(6);
    snapshotLabel_->setObjectName("snapshotSurface");
    liveLayout->addWidget(liveTable_, 1);
    tabs_->addTab(liveTab, "实时事件");

    auto* historyTab = new QWidget(tabs_);
    auto* historyLayout = new QVBoxLayout(historyTab);
    historyLayout->setContentsMargins(8, 8, 8, 8);
    auto* historyButtons = new QHBoxLayout();
    auto* refreshButton = new QPushButton("刷新", historyTab);
    connect(refreshButton, &QPushButton::clicked, this, [this] {
        refreshHistory();
    });
    auto* exportButton = new QPushButton("导出 CSV", historyTab);
    connect(exportButton, &QPushButton::clicked, this, [this] {
        exportHistory();
    });
    historyButtons->addWidget(refreshButton);
    historyButtons->addWidget(exportButton);
    historyButtons->addStretch(1);
    historyTable_ = createTable(
        {"时间", "车牌", "颜色", "截图"});
    connect(
        historyTable_,
        &QTableWidget::itemSelectionChanged,
        this,
        [this] { showHistorySnapshot(); });
    historyLayout->addLayout(historyButtons);
    historyLayout->addWidget(historyTable_, 1);
    tabs_->addTab(historyTab, "历史记录");
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int) {
        refreshHistory();
    });

    auto* rightPanel = new QWidget(splitter);
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(tabs_, 1);
    rightLayout->addWidget(snapshotLabel_, 0);
    splitter->addWidget(rightPanel);
    splitter->setSizes({940, 500});
    setCentralWidget(splitter);

    statusLabel_ = new QLabel("就绪", this);
    modelLabel_ = new QLabel("模型未启动", this);
    statusBar()->addWidget(statusLabel_, 1);
    statusBar()->addPermanentWidget(modelLabel_);

    connect(engineCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        settings_.engine = engineCombo_->currentData().toString();
    });
}

void MainWindow::applyStyle() {
    setStyleSheet(R"(
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
    )");
}

QTableWidget* MainWindow::createTable(const QStringList& headers) {
    auto* table = new QTableWidget(0, headers.size(), this);
    table->setHorizontalHeaderLabels(headers);
    table->setSelectionBehavior(QTableWidget::SelectRows);
    table->setEditTriggers(QTableWidget::NoEditTriggers);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
    return table;
}

void MainWindow::openSource() {
    const QString path = QFileDialog::getOpenFileName(
        this,
        "选择视频",
        QDir(rootDirectory()).filePath("output"),
        "Video (*.mp4 *.avi *.mov *.mkv);;Images (*.jpg *.jpeg *.png);;All (*)");
    if (!path.isEmpty()) {
        sourceEdit_->setText(path);
    }
}

void MainWindow::openSettings() {
    SettingsDialog dialog(settings_, this);
    if (dialog.exec() == QDialog::Accepted) {
        settings_ = dialog.resultSettings();
        engineCombo_->setCurrentIndex(
            qMax(0, engineCombo_->findData(settings_.engine)));
        saveSettings();
        modelLabel_->setText("设置已保存");
        refreshHistory();
    }
}

void MainWindow::startSession() {
    if (process_ && process_->state() != QProcess::NotRunning) {
        return;
    }
    const QString source = sourceEdit_->text().trimmed();
    if (source.isEmpty()) {
        QMessageBox::warning(
            this,
            "缺少视频源",
            "请选择视频文件或输入视频流地址。");
        return;
    }
    const QString cli = findCli();
    if (cli.isEmpty()) {
        QMessageBox::critical(
            this,
            "缺少后端",
            "未找到 plate_cli.exe，请先完成 Release 构建。");
        return;
    }

    settings_.source = source;
    settings_.engine = engineCombo_->currentData().toString();
    saveSettings();
    liveTable_->setRowCount(0);
    liveSnapshots_.clear();
    paused_ = false;
    snapshotLabel_->setText("暂无截图");
    snapshotLabel_->setPixmap(QPixmap());
    snapshotImage_ = QImage();
    stdoutBuffer_.clear();
    stderrText_.clear();
    previewImage_ = QImage();

    QStringList arguments = {
        "--input", source,
        "--engine", settings_.engine,
        "--ui-stream",
        "--control-stdin",
        "--database", settings_.database,
        "--snapshot-dir", settings_.snapshots,
        "--queue-capacity",
        source.startsWith("rtsp", Qt::CaseInsensitive) ? "4" : "8",
        "--fps", QString::number(settings_.fps)};
    arguments << "--provider" << settings_.provider
              << "--device-id" << QString::number(settings_.deviceId);

    if (settings_.engine == "hyperlpr") {
        arguments << "--detector-model" << settings_.detectorModel
                  << "--recognizer-model" << settings_.recognizerModel
                  << "--classifier-model" << settings_.classifierModel
                  << "--hyperlpr-tokens" << settings_.hyperlprTokens
                  << "--score-threshold" << "0.30"
                  << "--iou-threshold" << "0.60";
    } else if (settings_.engine == "onnx") {
        arguments << "--detector-model" << settings_.detectorModel
                  << "--recognizer-model" << settings_.recognizerModel
                  << "--alphabet" << settings_.alphabet;
    } else {
        arguments << "--plate" << settings_.mockPlate
                  << "--color" << "blue";
    }

    process_ = new QProcess(this);
    process_->setWorkingDirectory(rootDirectory());
    process_->setProcessChannelMode(QProcess::SeparateChannels);
    connect(
        process_,
        &QProcess::readyReadStandardOutput,
        this,
        [this] { readStdout(); });
    connect(
        process_,
        &QProcess::readyReadStandardError,
        this,
        [this] { readStderr(); });
    connect(
        process_,
        &QProcess::finished,
        this,
        [this](int, QProcess::ExitStatus) { processFinished(); });
    connect(
        process_,
        &QProcess::errorOccurred,
        this,
        [this](QProcess::ProcessError error) { processError(error); });
    process_->start(cli, arguments);
    setRunningState(true);
    if (!qFuzzyCompare(
            speedCombo_->currentData().toDouble(),
            1.0)) {
        process_->write(
            ("speed " +
             QString::number(speedCombo_->currentData().toDouble()) +
             "\n")
                .toUtf8());
    }
    statusLabel_->setText("正在启动模型");
    modelLabel_->setText(engineLabel());
}

void MainWindow::stopSession() {
    if (process_ && process_->state() != QProcess::NotRunning) {
        statusLabel_->setText("正在停止");
        process_->write("stop\n");
        if (!process_->waitForFinished(2000)) {
            process_->terminate();
        }
        if (process_->state() != QProcess::NotRunning &&
            !process_->waitForFinished(1000)) {
            process_->kill();
            process_->waitForFinished(1000);
        }
    }
}

void MainWindow::togglePause() {
    if (!process_ || process_->state() == QProcess::NotRunning) {
        return;
    }
    paused_ = !paused_;
    process_->write(paused_ ? "pause\n" : "resume\n");
    statusLabel_->setText(paused_ ? "已暂停" : "继续播放");
}

void MainWindow::setPlaybackSpeed(double speed) {
    if (!process_ || process_->state() == QProcess::NotRunning) {
        return;
    }
    process_->write(
        ("speed " + QString::number(speed) + "\n").toUtf8());
    statusLabel_->setText(
        QString("播放倍速 %1x").arg(speed));
}

void MainWindow::readStdout() {
    if (process_ == nullptr) {
        return;
    }
    stdoutBuffer_.append(process_->readAllStandardOutput());
    int newline = stdoutBuffer_.indexOf('\n');
    while (newline >= 0) {
        const QByteArray line = stdoutBuffer_.left(newline).trimmed();
        stdoutBuffer_.remove(0, newline + 1);
        if (!line.isEmpty()) {
            QJsonParseError parseError{};
            const QJsonDocument document =
                QJsonDocument::fromJson(line, &parseError);
            if (parseError.error == QJsonParseError::NoError &&
                document.isObject()) {
                handleMessage(document.object());
            }
        }
        newline = stdoutBuffer_.indexOf('\n');
    }
}

void MainWindow::readStderr() {
    if (process_ != nullptr) {
        stderrText_ += QString::fromUtf8(
            process_->readAllStandardError());
        if (stderrText_.size() > 6000) {
            stderrText_ = stderrText_.right(6000);
        }
    }
}

void MainWindow::handleMessage(const QJsonObject& message) {
    const QString type = message.value("type").toString();
    if (type == "status") {
        statusLabel_->setText(message.value("message").toString());
    } else if (type == "error") {
        statusLabel_->setText(
            "错误：" + message.value("message").toString());
    } else if (type == "frame") {
        updatePreview(message);
        const QJsonArray events = message.value("events").toArray();
        for (const QJsonValue& value : events) {
            addLiveEvent(value.toObject());
        }
    } else if (type == "finished") {
        statusLabel_->setText(
            QString("完成，处理 %1 帧，事件 %2 条")
                .arg(message.value("processed_frames").toInt())
                .arg(message.value("recognized_events").toInt()));
        refreshHistory();
    }
}

void MainWindow::updatePreview(const QJsonObject& message) {
    QImage image = decodeBase64Image(
        message.value("image_jpeg").toString());
    if (image.isNull()) {
        return;
    }
    const int frameWidth = message.value("frame_width").toInt(image.width());
    const int frameHeight = message.value("frame_height").toInt(image.height());
    const qreal scaleX =
        static_cast<qreal>(image.width()) / qMax(1, frameWidth);
    const qreal scaleY =
        static_cast<qreal>(image.height()) / qMax(1, frameHeight);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setFont(QFont("Microsoft YaHei", 11));
    const QJsonArray observations =
        message.value("observations").toArray();
    for (const QJsonValue& value : observations) {
        const QJsonObject observation = value.toObject();
        const QJsonObject box = observation.value("box").toObject();
        const QRectF rect(
            box.value("x").toDouble() * scaleX,
            box.value("y").toDouble() * scaleY,
            box.value("width").toDouble() * scaleX,
            box.value("height").toDouble() * scaleY);
        const QColor color = observation.value("valid").toBool()
            ? QColor("#16a34a")
            : QColor("#dc2626");
        painter.setPen(QPen(color, 3));
        painter.drawRect(rect);
        const QString label = observation.value("text").toString();
        if (!label.isEmpty()) {
            const QFontMetrics metrics(painter.font());
            const int textWidth = metrics.horizontalAdvance(label) + 10;
            const int textHeight = metrics.height() + 4;
            const int labelX = qBound(
                0,
                static_cast<int>(rect.left()),
                qMax(0, image.width() - textWidth));
            const int labelY = qMax(
                0,
                static_cast<int>(rect.top()) - textHeight);
            painter.fillRect(labelX, labelY, textWidth, textHeight, color);
            painter.setPen(Qt::white);
            painter.drawText(
                labelX + 5,
                labelY + metrics.ascent() + 2,
                label);
        }
    }
    painter.end();
    previewImage_ = image;
    frameSize_ = QSize(frameWidth, frameHeight);
    renderPreview();
}

void MainWindow::renderPreview() {
    if (previewImage_.isNull()) {
        return;
    }
    videoLabel_->setPixmap(
        QPixmap::fromImage(previewImage_).scaled(
            videoLabel_->size(),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation));
}

void MainWindow::addLiveEvent(const QJsonObject& event) {
    liveTable_->insertRow(0);
    const QStringList values = {
        formatTimestamp(event.value("captured_at_us").toVariant().toLongLong()),
        event.value("plate_number").toString(),
        event.value("color").toString(),
        QString::number(event.value("confidence").toDouble(), 'f', 3)};
    for (int column = 0; column < values.size(); ++column) {
        liveTable_->setItem(0, column, new QTableWidgetItem(values[column]));
    }
    const QImage snapshot = decodeBase64Image(
        event.value("snapshot_jpeg").toString());
    liveSnapshots_.prepend(snapshot);
    if (!snapshot.isNull()) {
        snapshotImage_ = snapshot;
        renderSnapshot();
    }
    if (liveTable_->currentRow() < 0) {
        liveTable_->setCurrentCell(0, 0);
    }
    if (smokeTest_ && !smokeEventSeen_) {
        smokeEventSeen_ = true;
        QDir().mkpath(QFileInfo(smokeScreenshot_).absolutePath());
        grab().save(smokeScreenshot_, "PNG");
        stopSession();
        QTimer::singleShot(500, this, [this] {
            tabs_->setCurrentIndex(1);
            refreshHistory();
            QTimer::singleShot(350, this, [this] {
                const QFileInfo info(smokeScreenshot_);
                const QString historyPath = info.dir().filePath(
                    info.completeBaseName() + "_history.png");
                grab().save(historyPath, "PNG");
                QApplication::quit();
            });
        });
    }
}

void MainWindow::processFinished() {
    paused_ = false;
    setRunningState(false);
    if (!statusLabel_->text().startsWith("完成")) {
        statusLabel_->setText("处理结束");
    }
    if (!stderrText_.trimmed().isEmpty()) {
        modelLabel_->setToolTip(stderrText_.right(2000));
    }
    refreshHistory();
}

void MainWindow::showLiveSnapshot() {
    const int row = liveTable_->currentRow();
    if (row < 0 || row >= liveSnapshots_.size()) {
        return;
    }
    const QImage image = liveSnapshots_[row];
    if (!image.isNull()) {
        snapshotImage_ = image;
        renderSnapshot();
    }
}

void MainWindow::processError(QProcess::ProcessError error) {
    if (error != QProcess::Crashed) {
        statusLabel_->setText(
            QString("后端启动失败：%1").arg(static_cast<int>(error)));
    }
}

void MainWindow::refreshHistory() {
    historyTable_->setRowCount(0);
    if (!QFileInfo::exists(settings_.database)) {
        return;
    }
    const QString connectionName =
        QString("history_%1").arg(reinterpret_cast<quintptr>(this), 0, 16);
    if (QSqlDatabase::contains(connectionName)) {
        QSqlDatabase::removeDatabase(connectionName);
    }
    {
        QSqlDatabase database =
            QSqlDatabase::addDatabase("QSQLITE", connectionName);
        database.setDatabaseName(settings_.database);
        if (!database.open()) {
            return;
        }
        QSqlQuery query(database);
        if (query.exec(
                "SELECT id, plate_number, plate_color, captured_at_ms, "
                "snapshot_path FROM recognition_events "
                "ORDER BY captured_at_ms DESC LIMIT 200")) {
            while (query.next()) {
                const int row = historyTable_->rowCount();
                historyTable_->insertRow(row);
                const QStringList values = {
                    QDateTime::fromMSecsSinceEpoch(
                        query.value(3).toLongLong())
                        .toString("yyyy-MM-dd HH:mm:ss"),
                    query.value(1).toString(),
                    query.value(2).toString(),
                    query.value(4).toString()};
                for (int column = 0; column < values.size(); ++column) {
                    auto* item = new QTableWidgetItem(values[column]);
                    item->setData(Qt::UserRole, query.value(0));
                    historyTable_->setItem(row, column, item);
                }
            }
        }
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
}

void MainWindow::showHistorySnapshot() {
    const QList<QTableWidgetItem*> selected =
        historyTable_->selectedItems();
    if (selected.isEmpty()) {
        return;
    }
    const int row = selected.first()->row();
    QTableWidgetItem* pathItem = historyTable_->item(row, 3);
    if (pathItem == nullptr || pathItem->text().isEmpty()) {
        return;
    }
    const QString path = QDir(settings_.snapshots).filePath(
        pathItem->text());
    const QImage image(path);
    if (!image.isNull()) {
        snapshotImage_ = image;
        renderSnapshot();
    }
}

void MainWindow::renderSnapshot() {
    if (snapshotImage_.isNull()) {
        return;
    }
    snapshotLabel_->setPixmap(
        QPixmap::fromImage(snapshotImage_).scaled(
            snapshotLabel_->size() - QSize(16, 16),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation));
}

void MainWindow::exportHistory() {
    const QString path = QFileDialog::getSaveFileName(
        this,
        "导出记录",
        QDir(rootDirectory()).filePath("output/records.csv"),
        "CSV (*.csv)");
    if (path.isEmpty()) {
        return;
    }
    const QString connectionName =
        QString("export_%1").arg(reinterpret_cast<quintptr>(this), 0, 16);
    if (QSqlDatabase::contains(connectionName)) {
        QSqlDatabase::removeDatabase(connectionName);
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "导出失败", "无法写入 CSV 文件。");
        return;
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << QChar(0xFEFF);
    stream << "id,camera_id,stream_id,track_id,plate_number,color,"
              "confidence,captured_at,snapshot_path\n";
    int count = 0;
    {
        QSqlDatabase database =
            QSqlDatabase::addDatabase("QSQLITE", connectionName);
        database.setDatabaseName(settings_.database);
        if (database.open()) {
            QSqlQuery query(database);
            if (query.exec(
                    "SELECT id, camera_id, stream_id, track_id, "
                    "plate_number, plate_color, confidence, "
                    "captured_at_ms, snapshot_path "
                    "FROM recognition_events ORDER BY captured_at_ms DESC")) {
                while (query.next()) {
                    stream
                        << query.value(0).toString() << ','
                        << query.value(1).toString() << ','
                        << query.value(2).toString() << ','
                        << query.value(3).toString() << ','
                        << csvEscape(query.value(4).toString()) << ','
                        << query.value(5).toString() << ','
                        << query.value(6).toString() << ','
                        << csvEscape(
                               QDateTime::fromMSecsSinceEpoch(
                                   query.value(7).toLongLong())
                                   .toString("yyyy-MM-dd HH:mm:ss"))
                        << ','
                        << csvEscape(query.value(8).toString()) << '\n';
                    ++count;
                }
            }
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    file.close();
    statusLabel_->setText(
        QString("已导出 %1 条记录").arg(count));
}

void MainWindow::finishSmokeTest() {
    QDir().mkpath(QFileInfo(smokeScreenshot_).absolutePath());
    grab().save(smokeScreenshot_, "PNG");
    stopSession();
    QApplication::quit();
}

void MainWindow::setRunningState(bool running) {
    if (auto* toolbar = findChild<QToolBar*>()) {
        const QList<QAction*> actions = toolbar->actions();
        for (QAction* action : actions) {
            if (!action->icon().isNull()) {
                action->setEnabled(
                    running ? action->text() == "停止" : true);
            }
        }
    }
    engineCombo_->setEnabled(!running);
    speedCombo_->setEnabled(running);
    sourceEdit_->setEnabled(!running);
}

void MainWindow::setInitialSource(const QString& source) {
    sourceEdit_->setText(source);
    settings_.source = source;
}

void MainWindow::setInitialEngine(const QString& engine) {
    const int index = engineCombo_->findData(engine);
    if (index >= 0) {
        engineCombo_->setCurrentIndex(index);
        settings_.engine = engine;
    }
}

void MainWindow::setSmokeScreenshot(const QString& path) {
    smokeScreenshot_ = path;
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    renderPreview();
    renderSnapshot();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == videoLabel_ &&
        event->type() == QEvent::MouseButtonRelease) {
        togglePause();
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    stopSession();
    QMainWindow::closeEvent(event);
}

