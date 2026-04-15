#ifndef MAPBRIDGE_H
#define MAPBRIDGE_H

#include <QObject>

// QwebChannel桥接类
class MapBridge: public QObject
{
    Q_OBJECT
public:
    MapBridge(QObject* parent = nullptr);

public slots:
    // JS调C++接口：地图加载完成回调
    void notifyMapReady();
    // JS调C++接口：用户在地图上点击要素
    void onFeatureClick(const QString &featureJson);

signals:
    // 推给JS：发送地图指令
    void pushToMap(const QString &jsonMsg);
    // 地图就绪信号
    void mapReady();
};

#endif // MAPBRIDGE_H
