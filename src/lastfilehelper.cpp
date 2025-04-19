#include "lastfilehelper.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>

LastFileHelper::LastFileHelper(QObject *parent)
    : QObject(parent)
{
    // Définir un emplacement pour le fichier de configuration
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(configDir);
    m_configPath = configDir + "/config.json";
}

QString LastFileHelper::loadLastFilePath()
{
    QFile file(m_configPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    
    QJsonObject obj = doc.object();
    return obj.value("lastFile").toString();
}

void LastFileHelper::saveLastFilePath(const QString &path)
{
    QJsonObject config;
    
    // Charger la config existante d'abord
    QFile readFile(m_configPath);
    if (readFile.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(readFile.readAll());
        readFile.close();
        config = doc.object();
    }
    
    // Mettre à jour le chemin du dernier fichier
    config["lastFile"] = path;
    config["lastUpdated"] = QDateTime::currentDateTime().toString();
    
    // Écrire le fichier de configuration
    QFile writeFile(m_configPath);
    if (writeFile.open(QIODevice::WriteOnly)) {
        writeFile.write(QJsonDocument(config).toJson());
        writeFile.close();
    }
}

QJsonObject LastFileHelper::loadCheckState()
{
    QFile file(m_configPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QJsonObject();
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    
    QJsonObject obj = doc.object();
    return obj.value("checkStates").toObject();
}

void LastFileHelper::saveCheckState(const QJsonObject& state)
{
    QJsonObject config;
    
    // Charger la config existante d'abord
    QFile readFile(m_configPath);
    if (readFile.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(readFile.readAll());
        readFile.close();
        config = doc.object();
    }
    
    // Mettre à jour l'état des cases à cocher
    config["checkStates"] = state;
    config["stateUpdated"] = QDateTime::currentDateTime().toString();
    
    // Écrire le fichier de configuration
    QFile writeFile(m_configPath);
    if (writeFile.open(QIODevice::WriteOnly)) {
        writeFile.write(QJsonDocument(config).toJson());
        writeFile.close();
    }
}

void LastFileHelper::saveCompilationOptions(bool compileChapter, bool compileDocument)
{
    QJsonObject config;
    
    // Charger la config existante d'abord
    QFile readFile(m_configPath);
    if (readFile.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(readFile.readAll());
        readFile.close();
        config = doc.object();
    }
    
    // Créer un sous-objet pour les options de compilation
    QJsonObject compileOptions;
    compileOptions["compileChapter"] = compileChapter;
    compileOptions["compileDocument"] = compileDocument;
    
    // Mettre à jour les options de compilation
    config["compilationOptions"] = compileOptions;
    
    // Écrire le fichier de configuration
    QFile writeFile(m_configPath);
    if (writeFile.open(QIODevice::WriteOnly)) {
        writeFile.write(QJsonDocument(config).toJson());
        writeFile.close();
    }
}

std::tuple<bool, bool> LastFileHelper::loadCompilationOptions()
{
    // Valeurs par défaut
    bool compileChapter = true;
    bool compileDocument = false;
    
    QFile file(m_configPath);
    if (!file.open(QIODevice::ReadOnly)) {
        // Retourner les valeurs par défaut si le fichier n'existe pas
        return std::make_tuple(compileChapter, compileDocument);
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    
    QJsonObject obj = doc.object();
    if (obj.contains("compilationOptions")) {
        QJsonObject options = obj["compilationOptions"].toObject();
            
        if (options.contains("compileChapter"))
            compileChapter = options["compileChapter"].toBool();
            
        if (options.contains("compileDocument"))
            compileDocument = options["compileDocument"].toBool();
    }
    
    return std::make_tuple(compileChapter, compileDocument);
}