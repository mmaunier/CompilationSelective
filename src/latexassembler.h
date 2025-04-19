#pragma once
#include <QObject>
#include <QString>
#include <QVector>
#include <QFileInfo>
#include <QTextEdit>
#include <QQueue>
#include <QMutex>
#include <QTemporaryDir>
#include "latexparser.h"
#include "latexmodel.h"
#include "processrunner.h"

class LatexAssembler : public QObject
{
    Q_OBJECT

public:
    explicit LatexAssembler(QObject* parent = nullptr);
    ~LatexAssembler();
    
    // Crée un fichier temporaire pour la compilation partielle
    QString createPartialDocument(const QString& mainFilePath, LatexModel* model);
    
    // Compile le document partiel
    void compilePartialDocument(const QString& tempFilePath, QTextEdit* outputWidget, 
                               bool compileChapter, bool compileDocument);
    
    // Renomme le fichier PDF généré
    QString renameFinalPdf(const QString& tempFilePath, const QString& mainFilePath);
    
    // Nouvelles méthodes pour la compilation des chapitres
    void compileChapters(LatexModel* model, QTextEdit* outputWidget);
    
    // Nouvelle méthode pour compiler le document complet
    void compileFullDocument(LatexModel* model, QTextEdit* outputWidget);

    // Arrête toutes les compilations en cours
    void stopCompilation();
    
    // Indique si une compilation est en cours
    bool isCompiling() const;

    // Getter pour le dernier PDF généré
    QString getLastPdfPath() const { return m_lastPdfPath; }

signals:
    void compilationStarted();
    void compilationProgress(int current, int total);
    void compilationFinished(bool success, const QString& pdfPath);
    void compilationError(const QString& errorMessage);

    // Nouveau signal pour indiquer qu'un PDF est disponible
    void pdfAvailable(const QString& pdfPath);
    void chapterCompilationStarted(const QString& chapterName);
    void chapterCompilationFinished(const QString& chapterName, bool success, const QString& pdfPath);
    void allChaptersCompiled();
    void fullDocumentCompilationStarted();
    void fullDocumentCompilationProgress(int current, int total);
    void fullDocumentCompilationFinished(bool success, const QString& pdfPath);

private slots:
    void processNextChapter();
    void processFullDocument();

private:
    QString extractPreamble(const QString& filePath);
    QVector<QPair<QString, QString>> collectSelectedFiles(LatexModel* model);
    QString createTempFile(const QString& preamble, const QVector<QPair<QString, QString>>& files);
    
    // Nouvelles méthodes privées pour la gestion des chapitres
    struct ChapterInfo {
        QString name;
        QString path;
        QVector<QPair<QString, QString>> files;
    };
    
    QVector<ChapterInfo> identifyChaptersToCompile(LatexModel* model);
    QString createChapterTempFile(const QString& preamble, const ChapterInfo& chapter);
    void compileChapter(const ChapterInfo& chapter, const QString& tempFilePath);
    QString renameChapterPdf(const QString& tempFilePath, const QString& chapterName);
    
    // Nouvelles méthodes privées pour la gestion du document complet
    QVector<QPair<QString, QString>> collectAllDocumentFiles(LatexModel* model);
    QString createFullDocumentTempFile(const QString& preamble, const QVector<QPair<QString, QString>>& files);
    QString renameFullDocumentPdf(const QString& tempFilePath);
    
    ProcessRunner* m_processRunner;
    ProcessRunner* m_chapterProcessRunner;
    ProcessRunner* m_fullDocumentProcessRunner;

    // Variables pour la compilation partielle
    QString m_currentTempFile;
    bool m_isCompiling;
    int m_compilationCount;
    QTextEdit* m_partialOutputWidget;
    
    // Variables pour la compilation des chapitres
    QString m_currentChapterTempFile; // Pour les chapitres (variable séparée)
    QString m_currentChapterName;     // Ajouté : nom du chapitre en cours de traitement
    bool m_isCompilingChapters;
    int m_chapterCompilationCount;
    QTextEdit* m_chapterOutputWidget;
    QQueue<ChapterInfo> m_chapterQueue;
    QMutex m_mutex;
    
    // Variables pour le document complet
    QString m_fullDocumentTempFile;
    bool m_isCompilingFullDocument;
    int m_fullDocumentCompilationCount;
    QTextEdit* m_fullDocumentOutputWidget;
    
    // Chemin du fichier principal et du dernier PDF généré
    QString m_mainFilePath;
    QString m_lastPdfPath;

    // Trois répertoires temporaires distincts
    static QTemporaryDir s_partielTempDir;  // Pour la compilation partielle
    static QTemporaryDir s_chapterTempDir;  // Pour la compilation des chapitres
    static QTemporaryDir s_documentTempDir; // Pour la compilation du document complet
    
    // Listes séparées pour suivre les fichiers temporaires
    QVector<QString> m_partielTempFiles;   // Fichiers temporaires partiels
    QVector<QString> m_chapterTempFiles;   // Fichiers temporaires de chapitres
    QVector<QString> m_documentTempFiles;  // Fichiers temporaires de document complet
};