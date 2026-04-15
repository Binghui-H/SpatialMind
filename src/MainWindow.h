#pragma once
#include <QMainWindow>
#include <QSplitter>
#include <QWebEngineView>
#include <QWebChannel>
#include <QLineEdit>
#include <QTextBrowser>
#include <QPushButton>
#include <QListWidget>
#include <QLabel>
#include <QToolButton>
#include <QCheckBox>

#include "MapBridge.h"
#include "LLMClient.h"
#include "GISEngine.h"
#include "QueryHistory.h"
#include "ConversationManager.h"
#include "SettingsDialog.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

private slots:
    void onSendClicked();
    void onMapReady();
    void onHistoryItemClicked(QListWidgetItem *item);

    void appendChat(const QString &role, const QString &text);
    void highlightFeatures(const QString &geojson, const QString &desc);
    void setBusy(bool busy);
    void refreshHistoryPanel();

    void onSettingsClicked();
    void onDbConfigChanged(const DbConfig &cfg);

private:
    void setupUI();
    void setupConnections();
    void connectDb(const DbConfig &cfg);
    void updateDbStatus(bool ok, const QString &dbName = {});

    // UI 控件
    QWebEngineView  *m_mapView       = nullptr;
    QWebChannel     *m_channel       = nullptr;
    QTextBrowser    *m_chatLog       = nullptr;
    QLineEdit       *m_input         = nullptr;
    QPushButton     *m_sendBtn       = nullptr;
    QListWidget     *m_history       = nullptr;
    QLabel          *m_dbStatus      = nullptr;
    QToolButton     *m_settingsBtn   = nullptr;
    QCheckBox       *m_chkSpatialCtx = nullptr;  // 主界面直接可见
    QCheckBox       *m_chkRag        = nullptr;  // 主界面直接可见

    // 业务层
    MapBridge           *m_bridge       = nullptr;
    LLMClient           *m_llm          = nullptr;
    GISEngine           *m_gis          = nullptr;
    QueryHistory        *m_queryHistory = nullptr;
    ConversationManager *m_conv         = nullptr;
};
