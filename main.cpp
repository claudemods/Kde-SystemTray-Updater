#include <QApplication>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QTimer>
#include <QFile>
#include <QDir>
#include <QSettings>
#include <QDialog>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QHBoxLayout>
#include <QStyle>
#include <QPainter>
#include <QFontDatabase>

class CountdownDialog : public QDialog {
    Q_OBJECT
public:
    CountdownDialog(QWidget *parent = nullptr) : QDialog(parent), countdown(5) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setFixedSize(200, 200);

        QVBoxLayout *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        iconLabel = new QLabel(this);
        iconLabel->setAlignment(Qt::AlignCenter);
        iconLabel->setPixmap(QPixmap(":/images/updated.svg").scaled(100, 100, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        layout->addWidget(iconLabel);

        countdownLabel = new QLabel(QString::number(countdown), this);
        countdownLabel->setAlignment(Qt::AlignCenter);
        QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        font.setPointSize(24);
        font.setBold(true);
        countdownLabel->setFont(font);
        countdownLabel->setStyleSheet("color: #24ffff;");
        layout->addWidget(countdownLabel);

        timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, &CountdownDialog::updateCountdown);
    }

    void startCountdown() {
        countdown = 5;
        countdownLabel->setText(QString::number(countdown));
        show();
        timer->start(1000);
    }

private slots:
    void updateCountdown() {
        countdown--;
        countdownLabel->setText(QString::number(countdown));

        if (countdown <= 0) {
            timer->stop();
            accept();
        }
    }

private:
    QLabel *iconLabel;
    QLabel *countdownLabel;
    QTimer *timer;
    int countdown;
};

class UpdateCompleteDialog : public QDialog {
    Q_OBJECT
public:
    UpdateCompleteDialog(QWidget *parent = nullptr) : QDialog(parent) {
        setWindowTitle("Update Complete");
        setFixedSize(400, 200);

        QVBoxLayout *layout = new QVBoxLayout(this);

        QLabel *messageLabel = new QLabel("System updates were installed successfully!", this);
        messageLabel->setAlignment(Qt::AlignCenter);
        messageLabel->setStyleSheet("font-size: 16px; color: #24ffff;");
        layout->addWidget(messageLabel);

        QLabel *questionLabel = new QLabel("Would you like to reboot now?", this);
        questionLabel->setAlignment(Qt::AlignCenter);
        questionLabel->setStyleSheet("font-size: 14px;");
        layout->addWidget(questionLabel);

        QHBoxLayout *buttonLayout = new QHBoxLayout();

        QPushButton *yesButton = new QPushButton("Yes", this);
        yesButton->setStyleSheet("color: #24ffff;");
        connect(yesButton, &QPushButton::clicked, [this]() {
            rebootRequested = true;
            accept();
        });

        QPushButton *noButton = new QPushButton("No", this);
        noButton->setStyleSheet("color: #24ffff;");
        connect(noButton, &QPushButton::clicked, this, &QDialog::reject);

        buttonLayout->addWidget(yesButton);
        buttonLayout->addWidget(noButton);
        layout->addLayout(buttonLayout);
    }

    bool shouldReboot() const { return rebootRequested; }

private:
    bool rebootRequested = false;
};

class UpdateChecker : public QSystemTrayIcon {
    Q_OBJECT
public:
    UpdateChecker(QObject *parent = nullptr) : QSystemTrayIcon(parent), updatesAvailable(false) {
        // Set SVG icons (only using the original three)
        noUpdatesIcon = QIcon(":/images/no-updates.svg");
        updatesAvailableIcon = QIcon(":/images/updates.svg");
        updatedIcon = QIcon(":/images/updated.svg");

        // Set initial icon
        setIcon(noUpdatesIcon);
        setToolTip("Update Checker - No updates available");

        // Create context menu
        menu = new QMenu();

        checkAction = menu->addAction("Check for updates");
        connect(checkAction, &QAction::triggered, this, &UpdateChecker::checkForUpdates);

        listAction = menu->addAction("List available updates");
        listAction->setEnabled(false);
        connect(listAction, &QAction::triggered, this, &UpdateChecker::listUpdates);

        updateAction = menu->addAction("Install updates");
        updateAction->setEnabled(false);
        connect(updateAction, &QAction::triggered, this, &UpdateChecker::installUpdates);

        menu->addSeparator();

        QAction *configAction = menu->addAction("Configuration");
        connect(configAction, &QAction::triggered, this, &UpdateChecker::showConfig);

        menu->addSeparator();

        QAction *aboutAction = menu->addAction("About");
        connect(aboutAction, &QAction::triggered, this, &UpdateChecker::showAbout);

        menu->addSeparator();

        QAction *quitAction = menu->addAction("Quit");
        connect(quitAction, &QAction::triggered, qApp, &QCoreApplication::quit);

        setContextMenu(menu);

        // Load configuration
        loadConfig();

        // Check for updates on first launch
        QTimer::singleShot(1000, this, &UpdateChecker::checkForUpdates);

        // Set up periodic checks if enabled
        if (autoCheckEnabled) {
            autoCheckTimer = new QTimer(this);
            connect(autoCheckTimer, &QTimer::timeout, this, &UpdateChecker::checkForUpdates);
            autoCheckTimer->start(autoCheckInterval * 60 * 1000);
        }

        // Initialize dialogs
        countdownDialog = new CountdownDialog();
        updateCompleteDialog = new UpdateCompleteDialog();
    }

private slots:
    void checkForUpdates() {
        currentDistro = detectDistribution();
        QString command;
        QStringList args;

        if (currentDistro == "arch" || currentDistro == "cachyos") {
            command = "checkupdates";
        }
        else if (currentDistro == "ubuntu" || currentDistro == "debian") {
            command = "apt";
            args << "list" << "--upgradable";
        }
        else if (currentDistro == "neon") {
            command = "pkcon";
            args << "get-updates";
        }
        else {
            showMessage("Error", "Unsupported distribution", QSystemTrayIcon::Warning, 5000);
            return;
        }

        QProcess process;
        process.start(command, args);
        process.waitForFinished();

        QString output = process.readAllStandardOutput();
        QString error = process.readAllStandardError();

        if ((currentDistro == "ubuntu" || currentDistro == "debian") &&
            error.contains("WARNING: apt does not have a stable CLI interface")) {
            error.clear();
            }

            if (!error.isEmpty()) {
                showMessage("Error", "Update check failed: " + error, QSystemTrayIcon::Critical, 5000);
                return;
            }

            if (output.trimmed().isEmpty() ||
                ((currentDistro == "ubuntu" || currentDistro == "debian") && output.startsWith("Listing..."))) {
                // No updates available
                updatesAvailable = false;
            availableUpdates.clear();
            setIcon(noUpdatesIcon);
            setToolTip("Update Checker - System up to date");
            listAction->setEnabled(false);
            updateAction->setEnabled(false);

            if (showNoUpdatesNotification) {
                showMessage("Update Checker", "System is up to date", QSystemTrayIcon::Information, 3000);
            }
                } else {
                    // Updates available
                    updatesAvailable = true;
                    availableUpdates = output;
                    updateCount = output.count('\n');
                    if (currentDistro == "ubuntu" || currentDistro == "debian") updateCount--;

                    setIcon(updatesAvailableIcon);
                    setToolTip(QString("Update Checker - %1 updates available").arg(updateCount));
                    listAction->setEnabled(true);
                    updateAction->setEnabled(true);

                    if (showUpdatesNotification) {
                        showUpdatePrompt();
                    }
                }
    }

    void listUpdates() {
        QDialog listDialog;
        listDialog.setWindowTitle(QString("Available Updates (%1 packages)").arg(updateCount));
        listDialog.resize(600, 400);

        QVBoxLayout *layout = new QVBoxLayout(&listDialog);

        QTextEdit *textEdit = new QTextEdit(&listDialog);
        textEdit->setPlainText(availableUpdates);
        textEdit->setReadOnly(true);
        textEdit->setLineWrapMode(QTextEdit::NoWrap);

        QFont font = textEdit->font();
        font.setFamily("Monospace");
        textEdit->setFont(font);

        QHBoxLayout *buttonLayout = new QHBoxLayout();
        QPushButton *installButton = new QPushButton("Install Updates", &listDialog);
        installButton->setStyleSheet("color: #24ffff;");
        connect(installButton, &QPushButton::clicked, [&]() {
            listDialog.accept();
            installUpdates();
        });

        QPushButton *closeButton = new QPushButton("Close", &listDialog);
        closeButton->setStyleSheet("color: #24ffff;");
        connect(closeButton, &QPushButton::clicked, &listDialog, &QDialog::accept);

        buttonLayout->addWidget(installButton);
        buttonLayout->addWidget(closeButton);

        layout->addWidget(new QLabel("The following updates are available:"));
        layout->addWidget(textEdit);
        layout->addLayout(buttonLayout);

        listDialog.exec();
    }

    void installUpdates() {
        QString command;
        QStringList args;

        if (currentDistro == "arch" || currentDistro == "cachyos") {
            command = "konsole";
            args << "-e" << "sudo" << "pacman" << "-Syu";
        }
        else if (currentDistro == "ubuntu" || currentDistro == "debian") {
            command = "konsole";
            args << "-e" << "bash" << "-c" << "sudo apt update && sudo apt upgrade -y";
        }
        else if (currentDistro == "neon") {
            command = "konsole";
            args << "-e" << "sudo" << "pkcon" << "update" << "-y";
        }

        // Start the process and monitor it
        terminalProcess = new QProcess(this);
        connect(terminalProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &UpdateChecker::onTerminalClosed);

        terminalProcess->start(command, args);

        // Show countdown dialog when updates start installing
        countdownDialog->startCountdown();

        // Keep the updates available icon during installation
        setIcon(updatesAvailableIcon);
        setToolTip("Update Checker - Installing updates...");
    }

    void onTerminalClosed(int exitCode, QProcess::ExitStatus exitStatus) {
        Q_UNUSED(exitCode);
        Q_UNUSED(exitStatus);

        // Clean up the process
        terminalProcess->deleteLater();
        terminalProcess = nullptr;

        // Show update complete dialog
        updateCompleteDialog->exec();

        if (updateCompleteDialog->shouldReboot()) {
            QProcess::startDetached("konsole -e sudo", QStringList() << "reboot");
        }

        // Check for updates again after terminal closes
        checkForUpdates();
    }

    void showConfig() {
        QDialog configDialog;
        configDialog.setWindowTitle("Update Checker Configuration");

        QVBoxLayout *layout = new QVBoxLayout(&configDialog);

        QCheckBox *autoCheckBox = new QCheckBox("Enable automatic update checking", &configDialog);
        autoCheckBox->setChecked(autoCheckEnabled);

        QSpinBox *intervalSpin = new QSpinBox(&configDialog);
        intervalSpin->setRange(15, 1440);
        intervalSpin->setValue(autoCheckInterval);
        intervalSpin->setSuffix(" minutes");

        QCheckBox *notifyUpdatesBox = new QCheckBox("Notify when updates are available", &configDialog);
        notifyUpdatesBox->setChecked(showUpdatesNotification);

        QCheckBox *notifyNoUpdatesBox = new QCheckBox("Notify when no updates are available", &configDialog);
        notifyNoUpdatesBox->setChecked(showNoUpdatesNotification);

        QPushButton *saveButton = new QPushButton("Save", &configDialog);
        saveButton->setStyleSheet("color: #24ffff;");

        layout->addWidget(autoCheckBox);
        layout->addWidget(new QLabel("Check interval:"));
        layout->addWidget(intervalSpin);
        layout->addWidget(notifyUpdatesBox);
        layout->addWidget(notifyNoUpdatesBox);
        layout->addWidget(saveButton);

        connect(saveButton, &QPushButton::clicked, [&]() {
            autoCheckEnabled = autoCheckBox->isChecked();
            autoCheckInterval = intervalSpin->value();
            showUpdatesNotification = notifyUpdatesBox->isChecked();
            showNoUpdatesNotification = notifyNoUpdatesBox->isChecked();

            if (autoCheckTimer) {
                autoCheckTimer->stop();
                if (autoCheckEnabled) {
                    autoCheckTimer->start(autoCheckInterval * 60 * 1000);
                }
            } else if (autoCheckEnabled) {
                autoCheckTimer = new QTimer(this);
                connect(autoCheckTimer, &QTimer::timeout, this, &UpdateChecker::checkForUpdates);
                autoCheckTimer->start(autoCheckInterval * 60 * 1000);
            }

            saveConfig();
            configDialog.accept();
        });

        configDialog.exec();
    }

private:
    void showUpdatePrompt() {
        QDialog promptDialog;
        promptDialog.setWindowTitle("Updates Available");
        promptDialog.setFixedSize(400, 200);

        QVBoxLayout *layout = new QVBoxLayout(&promptDialog);

        QLabel *messageLabel = new QLabel(QString("%1 updates are available").arg(updateCount), &promptDialog);
        messageLabel->setAlignment(Qt::AlignCenter);
        messageLabel->setStyleSheet("font-size: 16px; color: #24ffff;");
        layout->addWidget(messageLabel);

        QHBoxLayout *buttonLayout = new QHBoxLayout();

        QPushButton *installButton = new QPushButton("Install Now", &promptDialog);
        installButton->setStyleSheet("color: #24ffff;");
        connect(installButton, &QPushButton::clicked, [&]() {
            promptDialog.accept();
            installUpdates();
        });

        QPushButton *listButton = new QPushButton("View List", &promptDialog);
        listButton->setStyleSheet("color: #24ffff;");
        connect(listButton, &QPushButton::clicked, [&]() {
            promptDialog.accept();
            listUpdates();
        });

        QPushButton *laterButton = new QPushButton("Later", &promptDialog);
        laterButton->setStyleSheet("color: #24ffff;");
        connect(laterButton, &QPushButton::clicked, &promptDialog, &QDialog::reject);

        buttonLayout->addWidget(installButton);
        buttonLayout->addWidget(listButton);
        buttonLayout->addWidget(laterButton);

        layout->addLayout(buttonLayout);

        promptDialog.exec();
    }

    QString detectDistribution() {
        if (QFile::exists("/etc/arch-release")) {
            QFile osRelease("/etc/os-release");
            if (osRelease.open(QIODevice::ReadOnly)) {
                QString content = osRelease.readAll();
                if (content.contains("CachyOS")) {
                    return "cachyos";
                }
            }
            return "arch";
        }
        if (QFile::exists("/etc/debian_version")) {
            QFile osRelease("/etc/os-release");
            if (osRelease.open(QIODevice::ReadOnly)) {
                QString content = osRelease.readAll();
                if (content.contains("KDE neon")) return "neon";
                if (content.contains("Ubuntu")) return "ubuntu";
            }
            return "debian";
        }
        return "unknown";
    }

    void loadConfig() {
        QSettings settings;
        autoCheckEnabled = settings.value("autoCheckEnabled", true).toBool();
        autoCheckInterval = settings.value("autoCheckInterval", 60).toInt();
        showUpdatesNotification = settings.value("showUpdatesNotification", true).toBool();
        showNoUpdatesNotification = settings.value("showNoUpdatesNotification", false).toBool();
    }

    void saveConfig() {
        QSettings settings;
        settings.setValue("autoCheckEnabled", autoCheckEnabled);
        settings.setValue("autoCheckInterval", autoCheckInterval);
        settings.setValue("showUpdatesNotification", showUpdatesNotification);
        settings.setValue("showNoUpdatesNotification", showNoUpdatesNotification);
    }

    void showAbout() {
        QMessageBox aboutBox;
        aboutBox.setWindowTitle("About Update Checker");
        aboutBox.setText("System Update Checker\n\n"
        "Supported distributions:\n"
        "- Arch Linux (pacman)\n"
        "- CachyOS (pacman)\n"
        "- Ubuntu (apt)\n"
        "- Debian (apt)\n"
        "- KDE Neon (pkcon)\n\n"
        "claudemods Kde System Tray Updater v1.02");
        aboutBox.setStyleSheet("QLabel { color: #24ffff; }");
        aboutBox.exec();
    }

    QMenu *menu;
    QAction *checkAction;
    QAction *listAction;
    QAction *updateAction;
    QTimer *autoCheckTimer = nullptr;
    CountdownDialog *countdownDialog = nullptr;
    UpdateCompleteDialog *updateCompleteDialog = nullptr;
    QProcess *terminalProcess = nullptr;
    QString currentDistro;
    bool updatesAvailable;
    int updateCount;
    QString availableUpdates;
    bool autoCheckEnabled;
    int autoCheckInterval;
    bool showUpdatesNotification;
    bool showNoUpdatesNotification;
    QIcon noUpdatesIcon;
    QIcon updatesAvailableIcon;
    QIcon updatedIcon;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Update Checker");
    app.setOrganizationName("claudemods");

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        QMessageBox::critical(nullptr, "Error", "System tray not available");
        return 1;
    }

    UpdateChecker checker;
    checker.show();

    return app.exec();
}

#include "main.moc"
