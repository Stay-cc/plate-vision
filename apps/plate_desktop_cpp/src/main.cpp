#include "main_window.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    application.setApplicationName("Plate Vision");
    application.setOrganizationName("PlateVision");

    QCommandLineParser parser;
    parser.setApplicationDescription("Real-time plate recognition desktop");
    parser.addHelpOption();
    QCommandLineOption smokeOption("smoke-test", "Run automated UI smoke test");
    QCommandLineOption sourceOption("source", "Initial video source", "source");
    QCommandLineOption engineOption(
        "engine",
        "Initial engine: hyperlpr, onnx, or mock",
        "engine");
    QCommandLineOption screenshotOption(
        "screenshot",
        "Smoke-test screenshot path",
        "path");
    parser.addOption(smokeOption);
    parser.addOption(sourceOption);
    parser.addOption(engineOption);
    parser.addOption(screenshotOption);
    parser.process(application);

    const bool smoke = parser.isSet(smokeOption);
    MainWindow window(smoke);
    if (parser.isSet(sourceOption)) {
        window.setInitialSource(parser.value(sourceOption));
    }
    if (parser.isSet(engineOption)) {
        window.setInitialEngine(parser.value(engineOption));
    }
    if (parser.isSet(screenshotOption)) {
        window.setSmokeScreenshot(parser.value(screenshotOption));
    }
    window.show();
    if (smoke) {
        QTimer::singleShot(300, &window, [&window] {
            window.startSession();
        });
    }
    return application.exec();
}

