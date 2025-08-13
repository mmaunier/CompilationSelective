#include "processrunner.h"
#include <QDateTime>
#include <QScrollBar>
#include <QRegularExpression>

ProcessRunner::ProcessRunner(QObject* parent)
    : QObject(parent)
{
}

bool ProcessRunner::runCommand(const QString& program, const QStringList& arguments, 
                               QTextEdit* outputWidget, const QString& workingDir)
{
    // Nettoyer le processus précédent s'il existe
    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }
    
    m_outputWidget = outputWidget;
    m_fullOutput.clear();
    
    // IMPORTANT: Vider les buffers au début
    m_outputBuffer.clear();
    m_errorBuffer.clear();
    
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
    if (!m_process || !m_outputWidget) return;
    
    QByteArray data = m_process->readAllStandardOutput();
    QString output = QString::fromUtf8(data);
    m_fullOutput.append(output);
    
    // Ajouter au buffer
    m_outputBuffer.append(output);
    
    // Traiter les lignes complètes (séparées par \n)
    QStringList lines = m_outputBuffer.split('\n');
    
    // Garder la dernière partie (potentiellement incomplète) dans le buffer
    if (!m_outputBuffer.endsWith('\n')) {
        m_outputBuffer = lines.takeLast();
    } else {
        m_outputBuffer.clear();
    }
    
    // Traiter chaque ligne complète
    for (const QString& line : lines) {
        processAndDisplayLine(line);
    }
}

void ProcessRunner::onReadyReadStandardError()
{
    if (!m_process || !m_outputWidget) return;
    
    QByteArray data = m_process->readAllStandardError();
    QString output = QString::fromUtf8(data);
    m_fullOutput.append(output);
    
    // Même traitement pour stderr
    m_errorBuffer.append(output);
    
    QStringList lines = m_errorBuffer.split('\n');
    
    if (!m_errorBuffer.endsWith('\n')) {
        m_errorBuffer = lines.takeLast();
    } else {
        m_errorBuffer.clear();
    }
    
    for (const QString& line : lines) {
        processAndDisplayLine(line);
    }
}

void ProcessRunner::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    // Traiter ce qui reste dans les buffers
    if (!m_outputBuffer.isEmpty()) {
        processAndDisplayLine(m_outputBuffer);
        m_outputBuffer.clear();
    }
    
    if (!m_errorBuffer.isEmpty()) {
        processAndDisplayLine(m_errorBuffer);
        m_errorBuffer.clear();
    }
    
    // Message de fin
    QString statusMessage;
    if (exitCode == 0) {
        statusMessage = QString("\nProcessus terminé avec code : %1").arg(exitCode);
    } else {
        statusMessage = QString("\nProcessus terminé avec ERREUR (code : %1)").arg(exitCode);
    }
    
    processAndDisplayLine(statusMessage);
    
    m_lastExitCode = exitCode;
    emit processFinished(exitCode, exitStatus);
}

void ProcessRunner::processAndDisplayLine(const QString& line)
{
    if (!m_outputWidget) return;
    
    // Ne pas traiter les lignes complètement vides
    if (line.isEmpty()) {
        m_outputWidget->append("");
        return;
    }
    
    // Détection du message de succès
    QRegularExpression successPattern("Processus.*termin.*code.*0", 
                                     QRegularExpression::CaseInsensitiveOption);
    if (line.contains(successPattern)) {
        QTextCursor cursor = m_outputWidget->textCursor();
        cursor.movePosition(QTextCursor::End);
        
        QTextCharFormat successFormat;
        successFormat.setForeground(QBrush(QColor("#00a000")));
        successFormat.setFontWeight(QFont::Bold);
        cursor.insertText(line + "\n", successFormat);
        
        QTextCharFormat defaultFormat;
        defaultFormat.setForeground(QBrush(Qt::black));
        defaultFormat.setFontWeight(QFont::Normal);
        cursor.setCharFormat(defaultFormat);
        m_outputWidget->setTextCursor(cursor);
        
        QScrollBar* scrollBar = m_outputWidget->verticalScrollBar();
        scrollBar->setValue(scrollBar->maximum());
        return;
    }
    
    // Détection des erreurs et warnings avec des patterns améliorés
    bool isError = false;
    bool isWarning = false;
    
    // Patterns pour les erreurs LaTeX
    static const QStringList errorPatterns = {
        "^!\\s+",                    // Erreur LaTeX commençant par !
        "Emergency stop",
        "Fatal error",
        "File ended",
        "Runaway argument",
        "Double subscript",
        "Too many \\}",
        "Illegal unit",
        "cannot find",
        "not found"
    };
    
    // Patterns pour les warnings (y compris undefined)
    static const QStringList warningPatterns = {
        "Warning:",
        "warning:",
        "LaTeX Font Warning",
        "Package.*Warning",
        "Overfull",
        "Underfull",
        "undefined",              // Déplacé ici pour être en orange
        "Undefined",
        "hbox",
        "vbox",
        "Font shape.*undefined"   // Font warnings spécifiques
    };
    
    // Vérifier les patterns d'erreur
    for (const QString& pattern : errorPatterns) {
        QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
        if (line.contains(re)) {
            isError = true;
            break;
        }
    }
    
    // Si ce n'est pas une erreur, vérifier les warnings
    if (!isError) {
        for (const QString& pattern : warningPatterns) {
            if (line.contains(pattern, Qt::CaseInsensitive)) {
                isWarning = true;
                break;
            }
        }
    }
    
    // Appliquer le formatage
    QTextCursor cursor = m_outputWidget->textCursor();
    cursor.movePosition(QTextCursor::End);
    
    QTextCharFormat format;
    if (isError) {
        format.setForeground(QBrush(Qt::red));
    } else if (isWarning) {
        format.setForeground(QBrush(QColor("#ff8800"))); // Orange
    } else {
        format.setForeground(QBrush(Qt::black));
    }
    
    cursor.insertText(line + "\n", format);
    
    // Réinitialiser le format
    QTextCharFormat defaultFormat;
    defaultFormat.setForeground(QBrush(Qt::black));
    cursor.setCharFormat(defaultFormat);
    m_outputWidget->setTextCursor(cursor);
    
    // Auto-scroll
    QScrollBar* scrollBar = m_outputWidget->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
}