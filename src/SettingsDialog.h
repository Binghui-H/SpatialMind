#pragma once
#include <QDialog>
#include <QSettings>

class QLineEdit;
class QSpinBox;
class QPushButton;
class QListWidget;

struct DbConfig {
    QString host     = "localhost";
    int     port     = 5434;
    QString dbName   = "";
    QString user     = "postgres";
    QString password = "";
};

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    // 供 MainWindow 启动时读取上次成功连接
    static DbConfig loadDbConfig();
    // 供 MainWindow 启动时读取功能开关
    static bool     loadSpatialContext();
    static bool     loadUseRag();

signals:
    void dbConfigChanged(const DbConfig &cfg);

private slots:
    void onTestConnect();
    void onAccept();
    void onHistoryRowChanged(int row);
    void onClearHistory();

private:
    void setupUI();
    void loadLastConfig();
    void saveLastConfig(const DbConfig &cfg);
    void appendToHistory(const DbConfig &cfg);
    void refreshHistoryList();

    QListWidget *m_historyList    = nullptr;
    QLineEdit   *m_host           = nullptr;
    QSpinBox    *m_port           = nullptr;
    QLineEdit   *m_dbName         = nullptr;
    QLineEdit   *m_user           = nullptr;
    QLineEdit   *m_password       = nullptr;
    QPushButton *m_btnTest        = nullptr;
    QPushButton *m_btnOk          = nullptr;
    QPushButton *m_btnCancel      = nullptr;
    QPushButton *m_btnClearHist   = nullptr;

    static const QString ORG;
    static const QString APP;
};
