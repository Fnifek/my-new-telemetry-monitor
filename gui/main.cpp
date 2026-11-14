#include "mainwindow.h"

#include <QApplication>
#include <QTimer>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QString::fromUtf8("Cooling Telemetry Monitor"));
    QApplication::setApplicationVersion("1.0");
    QApplication::setStyle("Fusion");

    MainWindow window;
    window.show();

    const QStringList args = app.arguments();
    const int shotIndex = args.indexOf("--screenshot");
    if (shotIndex >= 0 && shotIndex + 1 < args.size()) {
        // Режим для проверки интерфейса: запускаем приём, через секунду
        // сохраняем окно в PNG и выходим.
        const QString path = args[shotIndex + 1];
        QTimer::singleShot(100, &window, &MainWindow::startAcquisition);
        QTimer::singleShot(1500, &window, [&window, &app, path]() {
            window.grab().save(path);
            app.quit();
        });
    } else if (args.contains("--smoke-test")) {
        // Режим для CI: окно живёт 2 секунды, конвейер успевает
        // принять и обработать первые отсчёты, после чего приложение выходит.
        QTimer::singleShot(100, &window, &MainWindow::startAcquisition);
        QTimer::singleShot(1800, &window, &MainWindow::stopAcquisition);
        QTimer::singleShot(2000, &app, &QCoreApplication::quit);
    }

    return app.exec();
}
