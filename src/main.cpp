#include <QApplication>
#include <QDir>
#include <QString>
#include <QStringList>
#include <QTextCodec>
#include <QThread>
#include <QtDebug>

#include "config.h"
#include "coreservices.h"
#include "errordialoghandler.h"
#include "mixxxapplication.h"
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include "mixxxmainwindow.h"
#endif
#include "qml/qmlapplication.h"
#include "sources/soundsourceproxy.h"
#include "util/cmdlineargs.h"
#include "util/console.h"
#include "util/logging.h"
#include "util/versionstore.h"

namespace {

// Exit codes
constexpr int kFatalErrorOnStartupExitCode = 1;
constexpr int kParseCmdlineArgsErrorExitCode = 2;

constexpr char kScaleFactorEnvVar[] = "QT_SCALE_FACTOR";
const QString kConfigGroup = QStringLiteral("[Config]");
const QString kScaleFactorKey = QStringLiteral("ScaleFactor");

int runMixxx(MixxxApplication* pApp, const CmdlineArgs& args) {
    const auto pCoreServices = std::make_shared<mixxx::CoreServices>(args, pApp);

    CmdlineArgs::Instance().parseForUserFeedback();

    int exitCode;
    // TODO: remove --qml command line option and make QML only available
    // with Qt6 when Mixxx can build with Qt6.
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    if (args.getQml()) {
#endif
        mixxx::qml::QmlApplication qmlApplication(pApp, pCoreServices);
        exitCode = pApp->exec();
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    } else {
        // This scope ensures that `MixxxMainWindow` is destroyed *before*
        // CoreServices is shut down. Otherwise a debug assertion complaining about
        // leaked COs may be triggered.
        MixxxMainWindow mainWindow(pCoreServices);
        pApp->processEvents();
        pApp->installEventFilter(&mainWindow);

        QObject::connect(pCoreServices.get(),
                &mixxx::CoreServices::initializationProgressUpdate,
                &mainWindow,
                &MixxxMainWindow::initializationProgressUpdate);
        pCoreServices->initialize(pApp);
        mainWindow.initialize();

        // If startup produced a fatal error, then don't even start the
        // Qt event loop.
        if (ErrorDialogHandler::instance()->checkError()) {
            exitCode = kFatalErrorOnStartupExitCode;
        } else {
            qDebug() << "Displaying main window";
            mainWindow.show();

            qDebug() << "Running Mixxx";
            exitCode = pApp->exec();
        }
    }
#endif
    return exitCode;
}

void adjustScaleFactor(CmdlineArgs* pArgs) {
    if (qEnvironmentVariableIsSet(kScaleFactorEnvVar)) {
        bool ok;
        const double f = qgetenv(kScaleFactorEnvVar).toDouble(&ok);
        if (ok && f > 0) {
            // The environment variable overrides the preferences option
            qDebug() << "Using" << kScaleFactorEnvVar << f;
            pArgs->setScaleFactor(f);
            return;
        }
    }
    // We cannot use SettingsManager, because it depends on MixxxApplication
    // but the scale factor is read during it's constructor.
    // QHighDpiScaling can not be used afterwards because it is private.
    // This means the following code may fail after down/upgrade ... a one time issue.

    // Read and parse the config file from the settings path
    auto config = ConfigObject<ConfigValue>(
            QDir(pArgs->getSettingsPath()).filePath(MIXXX_SETTINGS_FILE),
            QString(),
            QString());
    QString strScaleFactor = config.getValue(
            ConfigKey(kConfigGroup, kScaleFactorKey));
    double scaleFactor = strScaleFactor.toDouble();
    if (scaleFactor > 0) {
        qDebug() << "Using preferences ScaleFactor" << scaleFactor;
        qputenv(kScaleFactorEnvVar, strScaleFactor.toLocal8Bit());
        pArgs->setScaleFactor(scaleFactor);
    }
}

} // anonymous namespace

int main(int argc, char * argv[]) {
    Console console;

    // These need to be set early on (not sure how early) in order to trigger
    // logic in the OS X appstore support patch from QTBUG-16549.
    QCoreApplication::setOrganizationDomain("mixxx.org");

    // This needs to be set before initializing the QApplication.
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    // workaround for https://bugreports.qt.io/browse/QTBUG-84363
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0) && QT_VERSION < QT_VERSION_CHECK(5, 15, 1)
    qputenv("QV4_FORCE_INTERPRETER", QByteArrayLiteral("1"));
#endif
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    // Follow whatever factor the user has selected in the system settings
    // By default the value is always rounded to the nearest int.
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
            Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif

    // Setting the organization name results in a QDesktopStorage::DataLocation
    // of "$HOME/Library/Application Support/Mixxx/Mixxx" on OS X. Leave the
    // organization name blank.
    //QCoreApplication::setOrganizationName("Mixxx");

    QCoreApplication::setApplicationName(VersionStore::applicationName());
    QCoreApplication::setApplicationVersion(VersionStore::version());

    // Construct a list of strings based on the command line arguments
    CmdlineArgs& args = CmdlineArgs::Instance();
    if (!args.parse(argc, argv)) {
        return kParseCmdlineArgsErrorExitCode;
    }

    // If you change this here, you also need to change it in
    // ErrorDialogHandler::errorDialog(). TODO(XXX): Remove this hack.
    QThread::currentThread()->setObjectName("Main");

    // Create the ErrorDialogHandler in the main thread, otherwise it will be
    // created in the thread of the first caller to instance(), which may not be
    // the main thread. Bug #1748636.
    ErrorDialogHandler::instance();

#ifdef __APPLE__
    Sandbox::checkSandboxed();
#endif

    adjustScaleFactor(&args);

    MixxxApplication app(argc, argv);

#ifdef __APPLE__
    // TODO: At this point it is too late to provide the same settings path to all components
    // and too early to log errors and give users advises in their system language.
    // Calling this from main.cpp before the QApplication is initialized may cause a crash
    // due to potential QMessageBox invocations within migrateOldSettings().
    // Solution: Start Mixxx with default settings, migrate the preferences, and then restart
    // immediately.
    if (!args.getSettingsPathSet()) {
        CmdlineArgs::Instance().setSettingsPath(Sandbox::migrateOldSettings());
    }
#endif

#ifdef __APPLE__
    QDir dir(QApplication::applicationDirPath());
    // Set the search path for Qt plugins to be in the bundle's PlugIns
    // directory, but only if we think the mixxx binary is in a bundle.
    if (dir.path().contains(".app/")) {
        // If in a bundle, applicationDirPath() returns something formatted
        // like: .../Mixxx.app/Contents/MacOS
        dir.cdUp();
        dir.cd("PlugIns");
        qDebug() << "Setting Qt plugin search path to:" << dir.absolutePath();
        // asantoni: For some reason we need to do setLibraryPaths() and not
        // addLibraryPath(). The latter causes weird problems once the binary
        // is bundled (happened with 1.7.2 when Brian packaged it up).
        QApplication::setLibraryPaths(QStringList(dir.absolutePath()));
    }
#endif

    // When the last window is closed, terminate the Qt event loop.
    QObject::connect(&app, &MixxxApplication::lastWindowClosed, &app, &MixxxApplication::quit);

    int exitCode = runMixxx(&app, args);

    qDebug() << "Mixxx shutdown complete with code" << exitCode;

    mixxx::Logging::shutdown();

    return exitCode;
}
