#pragma once
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTextEdit>

class ProcessRunner : public QObject
{
    Q_OBJECT

public:
    explicit ProcessRunner(QObject* parent = nullptr);
    
    // Lance une commande et connecte sa sortie à un QTextEdit
    bool runCommand(const QString& program, const QStringList& arguments, 
                    QTextEdit* outputWidget, const QString& workingDir = QString());
    
    // Vérifie si la compilation nécessite d'être relancée
    bool needsRerun(const QString& output) const;
    
    // Stoppe le processus en cours
    void stopProcess();
    
    // Retourne le code de sortie du dernier processus
    int exitCode() const;
    
    // Retourne si le processus est en cours d'exécution
    bool isRunning() const;

    // Retourne la sortie complète du processus
    QString fullOutput() const { return m_fullOutput; }
    
    // Retourne le widget de sortie associé
    QTextEdit* outputWidget() const { return m_outputWidget; }

signals:
    // Signal émis lorsque le processus se termine
    void processFinished(int exitCode, QProcess::ExitStatus exitStatus);
    
    // Signal émis lorsqu'une nouvelle ligne est disponible
    void newOutputLine(const QString& line);

private slots:
    void onReadyReadStandardOutput();
    void onReadyReadStandardError();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    QProcess* m_process = nullptr;
    QTextEdit* m_outputWidget = nullptr;
    int m_lastExitCode = 0;
    QString m_fullOutput;
};