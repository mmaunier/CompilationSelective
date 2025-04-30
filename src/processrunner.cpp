#include "processrunner.h"
#include <QDateTime>
#include <QScrollBar>

ProcessRunner::ProcessRunner(QObject* parent)
    : QObject(parent)
{
}

bool ProcessRunner::runCommand(const QString& program, const QStringList& arguments, 
                               QTextEdit* outputWidget, const QString& workingDir)
{
    // Nettoyer le processus précédent s'il existe
    if (m_process) {
        if (m_process->state() != QProcess::NotRunning) {
            m_process->terminate();
            m_process->waitForFinished(3000);
        }
        delete m_process;
    }
    
    m_outputWidget = outputWidget;
    m_fullOutput.clear();
    
    // Créer un nouveau processus
    m_process = new QProcess(this);
    
    if (!workingDir.isEmpty()) {
        m_process->setWorkingDirectory(workingDir);
    }
   
    // Connecter les signaux pour capturer la sortie en temps réel
    connect(m_process, &QProcess::readyReadStandardOutput, this, &ProcessRunner::onReadyReadStandardOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, &ProcessRunner::onReadyReadStandardError);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), 
            this, &ProcessRunner::onProcessFinished);
    
    // Démarrer le processus
    m_process->start(program, arguments);
    return m_process->waitForStarted();
}

bool ProcessRunner::needsRerun(const QString& output) const
{
    // Mots-clés indiquant qu'une recompilation est nécessaire
    return output.contains("Rerun to get", Qt::CaseInsensitive) || 
           output.contains("Please rerun LaTeX", Qt::CaseInsensitive) ||
           output.contains("Rerun LaTeX", Qt::CaseInsensitive);
}

void ProcessRunner::stopProcess()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
    }
}

int ProcessRunner::exitCode() const
{
    return m_lastExitCode;
}

bool ProcessRunner::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

void ProcessRunner::onReadyReadStandardOutput()
{
    if (!m_process) return;
    
    QByteArray output = m_process->readAllStandardOutput();
    QString text = QString::fromUtf8(output);
    m_fullOutput += text;
    
    // Émettre le signal pour chaque ligne
    emit newOutputLine(text);
    
    // Ne plus ajouter directement au widget (ou en commentaire pour debug)
    // if (m_outputWidget) {
    //     QTextEdit* currentWidget = m_outputWidget;
    //     if (currentWidget) {
    //         currentWidget->append(text);
    //         currentWidget->verticalScrollBar()->setValue(
    //             currentWidget->verticalScrollBar()->maximum());
    //     }
    // }
}

void ProcessRunner::onReadyReadStandardError()
{
    if (!m_process) return;
    
    QByteArray output = m_process->readAllStandardError();
    QString text = QString::fromUtf8(output);
    m_fullOutput += text;
    
    // Émettre le signal pour chaque ligne
    emit newOutputLine(text);
    
    // Ne plus ajouter directement au widget (ou en commentaire pour debug)
    // if (m_outputWidget) {
    //    // ...même code commenté que ci-dessus
    // }
}

void ProcessRunner::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    m_lastExitCode = exitCode;
    emit processFinished(exitCode, exitStatus);
    
    // Créer le message de statut selon le code de sortie
    QString statusMessage;
    
    if (exitCode == 0) {
        // Code de sortie 0 = succès
        statusMessage = QString("\nProcessus terminé avec code : %1").arg(exitCode);
    } else {
        // Code de sortie non-zéro = échec (sera détecté comme une erreur par le mot "erreur")
        statusMessage = QString("\nProcessus terminé avec ERREUR (code : %1)").arg(exitCode);
    }
    
    // Émettre le signal pour que le message soit traité par highlightAndAppendLatexOutput
    emit newOutputLine(statusMessage);
}