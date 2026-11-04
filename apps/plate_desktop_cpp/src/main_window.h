#pragma once

#include <QByteArray>
#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QMainWindow>
#include <QProcess>
#include <QSize>
#include <QStringList>

class QComboBox;
class QCloseEvent;
class QEvent;
class QLabel;
class QLineEdit;
class QResizeEvent;
class QTabWidget;
class QTableWidget;
class QTimer;

struct DesktopSettings {
    QString source;
    QString engine;
    QString provider;
    int deviceId{0};
    QString detectorModel;
    QString recognizerModel;
    QString classifierModel;
    QString alphabet;
    QString hyperlprTokens;
    QString database;
    QString snapshots;
    QString mockPlate;
    int fps{25};
};

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(bool smokeTest, QWidget* parent = nullptr);
    ~MainWindow() override;

    void setInitialSource(const QString& source);
    void setInitialEngine(const QString& engine);
    void setSmokeScreenshot(const QString& path);
    void startSession();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    DesktopSettings defaultSettings() const;
    DesktopSettings loadSettings() const;
    void saveSettings();
    QString findCli() const;
    QString engineLabel() const;
    void buildUi();
    void applyStyle();
    QTableWidget* createTable(const QStringList& headers);
    void openSource();
    void openSettings();
    void stopSession();
    void readStdout();
    void readStderr();
    void handleMessage(const QJsonObject& message);
    void updatePreview(const QJsonObject& message);
    void renderPreview();
    void renderSnapshot();
    void addLiveEvent(const QJsonObject& event);
    void processFinished();
    void processError(QProcess::ProcessError error);
    void refreshHistory();
    void showLiveSnapshot();
    void showHistorySnapshot();
    void exportHistory();
    void finishSmokeTest();
    void setRunningState(bool running);
    void togglePause();
    void setPlaybackSpeed(double speed);

    bool smokeTest_{false};
    bool smokeEventSeen_{false};
    QString smokeScreenshot_;
    QTimer* smokeTimer_{nullptr};
    DesktopSettings settings_;
    QProcess* process_{nullptr};
    QByteArray stdoutBuffer_;
    QString stderrText_;
    QImage previewImage_;
    QImage snapshotImage_;
    QSize frameSize_;
    QList<QImage> liveSnapshots_;
    bool paused_{false};

    QLineEdit* sourceEdit_{nullptr};
    QComboBox* engineCombo_{nullptr};
    QComboBox* speedCombo_{nullptr};
    QLabel* videoLabel_{nullptr};
    QLabel* snapshotLabel_{nullptr};
    QLabel* statusLabel_{nullptr};
    QLabel* modelLabel_{nullptr};
    QTableWidget* liveTable_{nullptr};
    QTableWidget* historyTable_{nullptr};
    QTabWidget* tabs_{nullptr};
};

