#pragma once
#include <QObject>
#include <QString>
#include <QJsonObject>
#include <tuple>
#include "latexparser.h"

class LastFileHelper : public QObject
{
    Q_OBJECT
public:
    explicit LastFileHelper(QObject *parent = nullptr);
    
    // Méthodes existantes
    QString loadLastFilePath();
    void saveLastFilePath(const QString &path);
    QJsonObject loadCheckState();
    void saveCheckState(const QJsonObject& state);
    
    // Nouvelles méthodes pour les options de compilation
    void saveCompilationOptions(bool compileChapter, bool compileDocument);
    std::tuple<bool, bool> loadCompilationOptions();
    
private:
    QString m_configPath;
};