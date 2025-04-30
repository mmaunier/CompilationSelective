#include "latexassembler.h"
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QDateTime>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QRunnable>

// Initialisation des trois répertoires temporaires
QTemporaryDir LatexAssembler::s_partielTempDir(QDir::tempPath() + "/qt_partiel_temp-XXXXXX");
QTemporaryDir LatexAssembler::s_chapterTempDir(QDir::tempPath() + "/qt_chapter_temp-XXXXXX");
QTemporaryDir LatexAssembler::s_documentTempDir(QDir::tempPath() + "/qt_doc_temp-XXXXXX");

LatexAssembler::LatexAssembler(QObject* parent) : QObject(parent), m_isCompiling(false), m_isCompilingChapters(false), 
                                                m_isCompilingFullDocument(false), m_compilationCount(0), 
                                                m_chapterCompilationCount(0), m_fullDocumentCompilationCount(0),
                                                m_partialOutputWidget(nullptr), m_chapterOutputWidget(nullptr),
                                                m_fullDocumentOutputWidget(nullptr),
                                                m_processRunner(new ProcessRunner(this)),
                                                m_chapterProcessRunner(new ProcessRunner(this)),
                                                m_fullDocumentProcessRunner(new ProcessRunner(this))
{
    // S'assurer que les trois répertoires temporaires sont créés correctement
    if (!s_partielTempDir.isValid()) {
        qWarning() << "Impossible de créer le répertoire temporaire pour les documents partiels";
    } else {
        qDebug() << "Répertoire temporaire pour les documents partiels créé:" << s_partielTempDir.path();
    }
    
    if (!s_chapterTempDir.isValid()) {
        qWarning() << "Impossible de créer le répertoire temporaire pour les chapitres";
    } else {
        qDebug() << "Répertoire temporaire pour les chapitres créé:" << s_chapterTempDir.path();
    }
    
    if (!s_documentTempDir.isValid()) {
        qWarning() << "Impossible de créer le répertoire temporaire pour le document complet";
    } else {
        qDebug() << "Répertoire temporaire pour le document complet créé:" << s_documentTempDir.path();
    }

    // Connecteur UNIQUEMENT pour le processus de document partiel
    connect(m_processRunner, &ProcessRunner::processFinished, this, [this](int exitCode, QProcess::ExitStatus exitStatus)
    {
        qDebug() << "Processus PARTIEL terminé avec code:" << exitCode;
        
        // Si le processus s'est terminé avec une erreur
        if (exitCode != 0) {
            m_isCompiling = false;
            emit compilationError("Erreur LaTeX détectée dans le document partiel. Code de sortie: " + QString::number(exitCode));
            emit compilationFinished(false, "");
            return;
        }
        
        // Vérification explicite du document partiel
        QString fullOutput = m_processRunner->fullOutput();
        bool needsRerun = m_processRunner->needsRerun(fullOutput);
        qDebug() << "Document partiel: besoin de recompiler =" << needsRerun << "(compilation" << m_compilationCount << "sur 5)";
        
        // Si le processus s'est terminé normalement mais nécessite une recompilation
        if (m_compilationCount < 5 && needsRerun) {
            m_compilationCount++;
            
            // Ajouter un séparateur entre les compilations
            if (m_partialOutputWidget) {
                m_partialOutputWidget->append("\n\n***********************************************");
                m_partialOutputWidget->append(QString("************* %1-ième compilation du document partiel *************").arg(m_compilationCount));
                m_partialOutputWidget->append("***********************************************\n\n");
            }
            
            // Relancer la compilation du document partiel
            QFileInfo tempFileInfo(m_currentTempFile);
            QStringList args;
            args << "-synctex=1"
                 << "-shell-escape"
                 << "-interaction=nonstopmode"
                 << "-file-line-error"
                 << m_currentTempFile;
            
            m_processRunner->runCommand("lualatex", args, m_partialOutputWidget, tempFileInfo.absolutePath());
            emit compilationProgress(m_compilationCount, 5);
        }
        else {
            m_isCompiling = false;
            
            if (exitCode == 0) {
                // Renommer le fichier PDF final
                QFileInfo tempFileInfo(m_currentTempFile);
                QString baseName = tempFileInfo.completeBaseName();
                QString pdfPath = tempFileInfo.absolutePath() + "/" + baseName + ".pdf";
                
                if (QFile::exists(pdfPath)) {
                    QString finalPdfName = renameFinalPdf(m_currentTempFile, m_mainFilePath);
                    if (!finalPdfName.isEmpty()) {
                        emit compilationFinished(true, finalPdfName);
                        emit pdfAvailable(finalPdfName);
                    } else {
                        emit compilationError("Erreur lors du renommage du PDF");
                    }
                }
                else {
                    emit compilationError("Le fichier PDF n'a pas été généré");
                }
            }
            else {
                emit compilationFinished(false, "");
            }
        }
    }, Qt::QueuedConnection);
    
    // Connecteur UNIQUEMENT pour le processus de chapitres - complètement séparé
    connect(m_chapterProcessRunner, &ProcessRunner::processFinished, this, [this](int exitCode, QProcess::ExitStatus exitStatus)
    {
        qDebug() << "Processus CHAPITRE terminé avec code:" << exitCode;
        
        // Si le processus s'est terminé avec une erreur
        if (exitCode != 0) {
            if (m_chapterOutputWidget) {
                m_chapterOutputWidget->append("\n\n*** ERREUR dans la compilation du chapitre " + 
                                         m_currentChapterName + " (code " + QString::number(exitCode) + ") ***\n");
            }
            emit chapterCompilationFinished(m_currentChapterName, false, "");
            
            // Passer au chapitre suivant MÊME EN CAS D'ERREUR
            QMetaObject::invokeMethod(this, "processNextChapter", Qt::QueuedConnection);
            return;
        }
        
        // Vérification de recompilation du chapitre
        QString fullOutput = m_chapterProcessRunner->fullOutput();
        bool needsRerun = m_chapterProcessRunner->needsRerun(fullOutput);
        qDebug() << "Chapitre" << m_currentChapterName << ": besoin de recompiler =" << needsRerun;
        
        if (exitCode == 0 && m_chapterCompilationCount < 5 && needsRerun) {
            m_chapterCompilationCount++;
            
            // Ajouter un séparateur entre les compilations
            if (m_chapterOutputWidget) {
                m_chapterOutputWidget->append("\n\n***********************************************");
                m_chapterOutputWidget->append(QString("************* %1-ième compilation de %2 *************")
                                         .arg(m_chapterCompilationCount).arg(m_currentChapterName));
                m_chapterOutputWidget->append("***********************************************\n\n");
            }
            
            // Relancer la compilation du chapitre
            QFileInfo tempFileInfo(m_currentChapterTempFile);
            QStringList args;
            args << "-synctex=1"
                 << "-shell-escape"
                 << "-interaction=nonstopmode"
                 << "-file-line-error"
                 << m_currentChapterTempFile;
            
            m_chapterProcessRunner->runCommand("lualatex", args, m_chapterOutputWidget, tempFileInfo.absolutePath());
        }
        else {
            // Fin de la compilation pour ce chapitre
            bool success = (exitCode == 0);
            
            if (success) {
                // Renommer le PDF généré
                QFileInfo tempFileInfo(m_currentChapterTempFile);
                QString baseName = tempFileInfo.completeBaseName();
                QString pdfPath = tempFileInfo.absolutePath() + "/" + baseName + ".pdf";
                
                if (QFile::exists(pdfPath)) {
                    QString finalPdfName = renameChapterPdf(m_currentChapterTempFile, m_currentChapterName);
                    if (!finalPdfName.isEmpty()) {
                        emit chapterCompilationFinished(m_currentChapterName, true, finalPdfName);
                        qDebug() << "Compilation du chapitre" << m_currentChapterName << "terminée avec succès";
                    } else {
                        emit chapterCompilationFinished(m_currentChapterName, false, "");
                        qDebug() << "Erreur lors du renommage du PDF du chapitre" << m_currentChapterName;
                    }
                } else {
                    emit chapterCompilationFinished(m_currentChapterName, false, "");
                    qDebug() << "Le fichier PDF n'a pas été généré pour le chapitre" << m_currentChapterName;
                }
            }
            else {
                emit chapterCompilationFinished(m_currentChapterName, false, "");
            }
            
            // Passer au chapitre suivant
            QMetaObject::invokeMethod(this, "processNextChapter", Qt::QueuedConnection);
        }
    }, Qt::QueuedConnection);

    // Connecteur UNIQUEMENT pour le processus de document complet
    connect(m_fullDocumentProcessRunner, &ProcessRunner::processFinished, this, [this](int exitCode, QProcess::ExitStatus exitStatus)
    {
        qDebug() << "Processus DOCUMENT COMPLET terminé avec code:" << exitCode;
        
        // Si le processus s'est terminé avec une erreur
        if (exitCode != 0) {
            m_isCompilingFullDocument = false;
            emit compilationError("Erreur LaTeX détectée dans le document complet. Code de sortie: " + QString::number(exitCode));
            emit fullDocumentCompilationFinished(false, "");
            return;
        }
        
        // Vérification de recompilation
        QString fullOutput = m_fullDocumentProcessRunner->fullOutput();
        bool needsRerun = m_fullDocumentProcessRunner->needsRerun(fullOutput);
        qDebug() << "Document complet: besoin de recompiler =" << needsRerun << "(compilation" << m_fullDocumentCompilationCount << "sur 5)";
        
        if (m_fullDocumentCompilationCount < 5 && needsRerun) {
            m_fullDocumentCompilationCount++;
            
            // Ajouter un séparateur entre les compilations
            if (m_fullDocumentOutputWidget) {
                m_fullDocumentOutputWidget->append("\n\n***********************************************");
                m_fullDocumentOutputWidget->append(QString("************* %1-ième compilation du document complet *************").arg(m_fullDocumentCompilationCount));
                m_fullDocumentOutputWidget->append("***********************************************\n\n");
            }
            
            // Relancer la compilation du document complet
            QFileInfo tempFileInfo(m_fullDocumentTempFile);
            QStringList args;
            args << "-synctex=1"
                 << "-shell-escape"
                 << "-interaction=nonstopmode"
                 << "-file-line-error"
                 << m_fullDocumentTempFile;
            
            m_fullDocumentProcessRunner->runCommand("lualatex", args, m_fullDocumentOutputWidget, tempFileInfo.absolutePath());
            emit fullDocumentCompilationProgress(m_fullDocumentCompilationCount, 5);
        }
        else {
            m_isCompilingFullDocument = false;
            
            if (exitCode == 0) {
                // Renommer le fichier PDF final
                QFileInfo tempFileInfo(m_fullDocumentTempFile);
                QString baseName = tempFileInfo.completeBaseName();
                QString pdfPath = tempFileInfo.absolutePath() + "/" + baseName + ".pdf";
                
                if (QFile::exists(pdfPath)) {
                    QString finalPdfName = renameFullDocumentPdf(m_fullDocumentTempFile);
                    if (!finalPdfName.isEmpty()) {
                        emit fullDocumentCompilationFinished(true, finalPdfName);
                        emit pdfAvailable(finalPdfName);
                    } else {
                        emit fullDocumentCompilationFinished(false, "");
                    }
                }
                else {
                    emit compilationError("Le fichier PDF n'a pas été généré pour le document complet");
                    emit fullDocumentCompilationFinished(false, "");
                }
            }
            else {
                emit fullDocumentCompilationFinished(false, "");
            }
        }
    }, Qt::QueuedConnection);

    // Connexions pour la sortie brute
    connect(m_processRunner, &ProcessRunner::newOutputLine, this, [this](const QString& line){
        emit rawPartialOutputReady(line);
    });

    connect(m_chapterProcessRunner, &ProcessRunner::newOutputLine, this, [this](const QString& line){
        emit rawChapterOutputReady(line);
    });

    connect(m_fullDocumentProcessRunner, &ProcessRunner::newOutputLine, this, [this](const QString& line){
        emit rawDocumentOutputReady(line);
    });
}

LatexAssembler::~LatexAssembler()
{
    // Nettoyer les fichiers temporaires partiels
    for (const QString& tempFile : m_partielTempFiles) {
        if (QFile::exists(tempFile)) {
            QFile::remove(tempFile);
            
            // Nettoyer aussi le PDF correspondant s'il existe
            QString pdfFile = tempFile;
            pdfFile.replace(".tex", ".pdf");
            if (QFile::exists(pdfFile)) {
                QFile::remove(pdfFile);
            }
        }
    }
    
    // Nettoyer les fichiers temporaires des chapitres
    for (const QString& tempFile : m_chapterTempFiles) {
        if (QFile::exists(tempFile)) {
            QFile::remove(tempFile);
            
            // Nettoyer aussi le PDF correspondant s'il existe
            QString pdfFile = tempFile;
            pdfFile.replace(".tex", ".pdf");
            if (QFile::exists(pdfFile)) {
                QFile::remove(pdfFile);
            }
        }
    }
    
    // Nettoyer les fichiers temporaires du document complet
    for (const QString& tempFile : m_documentTempFiles) {
        if (QFile::exists(tempFile)) {
            QFile::remove(tempFile);
            
            // Nettoyer aussi le PDF correspondant s'il existe
            QString pdfFile = tempFile;
            pdfFile.replace(".tex", ".pdf");
            if (QFile::exists(pdfFile)) {
                QFile::remove(pdfFile);
            }
        }
    }
    
    // Les répertoires temporaires seront nettoyés automatiquement
    // par les destructeurs de s_partielTempDir, s_chapterTempDir et s_documentTempDir
}

QString LatexAssembler::extractPreamble(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    
    QTextStream in(&file);
    QString preamble;
    QString line;
    
    // Lire jusqu'à trouver \begin{document}
    while (!in.atEnd()) {
        line = in.readLine();
        if (line.contains("\\begin{document}")) {
            break;
        }
        preamble += line + "\n";
    }
    
    file.close();
    return preamble;
}

QVector<QPair<QString, QString>> LatexAssembler::collectSelectedFiles(LatexModel* model)
{
    QVector<QPair<QString, QString>> selectedFiles;
    
    // Parcourir le modèle pour trouver les fichiers sélectionnés
    // Cette fonction est une simplification - vous devez l'adapter à la structure de votre modèle
    std::function<void(const QModelIndex&)> collectFiles = [&](const QModelIndex& parent) {
        for (int i = 0; i < model->rowCount(parent); ++i) {
            QModelIndex index = model->index(i, 0, parent);
            Qt::CheckState state = static_cast<Qt::CheckState>(model->data(index, Qt::CheckStateRole).toInt());
            
            QString nodeName = model->data(index, Qt::DisplayRole).toString();
            QString nodePath = model->data(index, Qt::ToolTipRole).toString();
            
            // Ignorer les nœuds de groupe (PEDA, DOCS, EVALS)
            if (nodeName == "PEDA" || nodeName == "DOCS" || nodeName == "EVALS") {
                // Parcourir les enfants de ces groupes
                collectFiles(index);
            }
            else if (state == Qt::Checked && !nodePath.isEmpty() && QFile::exists(nodePath)) {
                // Ajouter le fichier à la liste s'il est coché et existe
                selectedFiles.append(qMakePair(nodeName, nodePath));
            }
            
            // Récursivement traiter les enfants si ce n'est pas un groupe
            if (model->hasChildren(index) && nodeName != "PEDA" && nodeName != "DOCS" && nodeName != "EVALS") {
                collectFiles(index);
            }
        }
    };
    
    // Commencer la collecte depuis la racine
    collectFiles(QModelIndex());
    
    return selectedFiles;
}

QString LatexAssembler::createPartialDocument(const QString& filePath, LatexModel* model)
{
    // Stockez le chemin du fichier principal pour référence future
    m_mainFilePath = filePath;
    
    // Extraire le préambule
    QString preamble = extractPreamble(filePath);
    if (preamble.isEmpty()) {
        emit compilationError("Impossible de lire le préambule du document");
        return QString();
    }
    
    // Collecter les nœuds sélectionnés
    QVector<QPair<QString, QString>> selectedFiles = collectSelectedFiles(model);
    if (selectedFiles.isEmpty()) {
        emit compilationError("Aucun fichier sélectionné");
        return QString();
    }
    
    // Créer un fichier temporaire dans le répertoire des documents partiels
    if (!s_partielTempDir.isValid()) {
        emit compilationError("Impossible de créer un répertoire temporaire");
        return QString();
    }
    
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    QString tempFileName = s_partielTempDir.path() + "/temp_partial_" + timestamp + ".tex";
    
    // Créer le fichier et écrire son contenu
    QFile tempFile(tempFileName);
    if (!tempFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit compilationError("Impossible de créer le fichier temporaire");
        return QString();
    }
    
    QTextStream out(&tempFile);
    
    // Écrire le préambule
    out << preamble;
    
    // Obtenir le chemin absolu du dossier contenant le fichier principal
    QFileInfo mainFileInfo(filePath);
    QString mainDirPath = mainFileInfo.absolutePath();
    
    // Ajouter la configuration des chemins d'images
    out << "% Configuration des chemins d'images pour le document temporaire\n";
    out << "\\graphicspath{{"
        << QDir::toNativeSeparators(mainDirPath + "/images/").replace("\\", "/") << "}{"
        << QDir::toNativeSeparators(mainDirPath + "/../images/").replace("\\", "/") << "}}\n\n";
    
    // Ajouter la configuration des en-têtes et pieds de page
    out << "% Configuration des en-têtes et pieds de page\n";
    out << "\\lhead{\\textcolor{gris50}{\\small\\textit{\\hyperlink{debut}{\\monetablissement}}}} %haut de page gauche\n";
    out << "\\chead{} %haut de page centre\n";
    out << "\\rhead{\\textcolor{gris50}{\\small\\textit{\\hyperlink{debut}{\\maclasse}}}} %haut de page droit\n";
    out << "\\lfoot{} %pied de page gauche\n";
    out << "\\cfoot{\\textcolor{gris50}{\\small\\textit{page \\thepage}}} % pied de page centré\n";
    out << "\\rfoot{} %On personnalisera cette en-tête\n";
    out << "\\def\\headrulewidth{0pt} %Trace un trait de séparation de largeur 0,4 point. Mettre 0pt pour supprimer le trait.\n";
    out << "\\def\\footrulewidth{0pt} %Trace un trait de séparation de largeur 0,4 point. Mettre 0pt pour supprimer le trait.\n";
    out << "\\pagestyle{empty}\n\n";
    
    // Début du document
    out << "\\begin{document}\n\n";
    
    // Ajouter la configuration de l'espacement
    out << "\\singlespacing\\setlength{\\parindent}{0pt}\n\n";
    
    // Ajouter le contenu des fichiers sélectionnés
    for (const auto& file : selectedFiles) {
        QFile inputFile(file.second);
        if (inputFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            out << "% Contenu du fichier: " << file.second << "\n";
            out << inputFile.readAll() << "\n\n";
            inputFile.close();
        }
    }
    
    // Fin du document
    out << "\\end{document}\n";
    
    tempFile.close();
    
    qDebug() << "Fichier temporaire partiel créé:" << tempFileName;
    
    // Ajouter à la liste des fichiers temporaires partiels
    m_partielTempFiles.append(tempFileName);
    
    return tempFileName;
}

void LatexAssembler::compilePartialDocument(const QString& tempFilePath, QTextEdit* outputWidget, 
                                           bool compileChapter, bool compileDocument)
{
    // Mettre à jour l'état et les variables
    m_isCompiling = true;
    m_currentTempFile = tempFilePath;
    m_compilationCount = 1;
    
    // IMPORTANT: Garder une référence explicite au widget de sortie pour éviter le mélange
    m_partialOutputWidget = outputWidget;
    
    emit compilationStarted();
    emit compilationProgress(m_compilationCount, 5);
    
    // Configuration des arguments pour LuaLaTeX
    QStringList args;
    args << "-synctex=1"
         << "-shell-escape"
         << "-interaction=nonstopmode"
         << "-file-line-error"
         << tempFilePath;
    
    QFileInfo tempFileInfo(tempFilePath);
    
    // Lancer le processus
    m_processRunner->runCommand("lualatex", args, m_partialOutputWidget, tempFileInfo.absolutePath());
}

QString LatexAssembler::renameFinalPdf(const QString& tempFilePath, const QString& mainFilePath)
{
    // Obtenir les informations sur les fichiers
    QFileInfo tempFileInfo(tempFilePath);
    QFileInfo mainFileInfo(mainFilePath);
    
    // S'assurer qu'on utilise le vrai chemin du document principal
    if (mainFilePath.isEmpty() || !mainFileInfo.exists()) {
        return QString(); // Impossible de renommer sans chemin valide
    }
    
    // Récupérer le nom de fichier principal (sans extension)
    QString baseName = mainFileInfo.completeBaseName();
    
    // Générer un timestamp formaté
    QString timestamp = QDateTime::currentDateTime().toString("yyyy_MM_dd_HH_mm_ss");
    
    // Chemin du nouveau fichier PDF dans le même répertoire que le document principal
    QString newPdfName = mainFileInfo.absolutePath() + "/" + baseName + "_partiel_" + timestamp + ".pdf";
    
    // Chemin du PDF original généré
    QString originalPdf = tempFileInfo.absolutePath() + "/" + tempFileInfo.completeBaseName() + ".pdf";
    
    if (QFile::exists(originalPdf)) {
        // Copier le fichier vers sa destination finale
        if (QFile::copy(originalPdf, newPdfName)) {
            // Supprimer l'original si la copie a réussi
            QFile::remove(originalPdf);
            
            // Stocker le chemin du dernier PDF généré
            m_lastPdfPath = newPdfName;
            
            return newPdfName;
        }
    }
    
    return QString(); // Retourner une chaîne vide en cas d'échec
}

void LatexAssembler::stopCompilation()
{
    if (m_isCompiling) {
        m_processRunner->stopProcess();
        m_isCompiling = false;
    }
    
    if (m_isCompilingChapters) {
        m_chapterProcessRunner->stopProcess();
        m_isCompilingChapters = false;
    }
    
    if (m_isCompilingFullDocument) {
        m_fullDocumentProcessRunner->stopProcess();
        m_isCompilingFullDocument = false;
    }
    
    // Vider la file d'attente
    QMutexLocker locker(&m_mutex);
    m_chapterQueue.clear();
}

bool LatexAssembler::isCompiling() const
{
    return m_isCompiling;
}

QVector<LatexAssembler::ChapterInfo> LatexAssembler::identifyChaptersToCompile(LatexModel* model)
{
    QVector<ChapterInfo> chapters;
    QMap<QString, ChapterInfo> chapterMap; // Pour regrouper les fichiers par chapitre
    QSet<QString> chaptersToCompile; // Pour marquer les chapitres qui doivent être compilés
    
    // Première passe : identifier les chapitres qui ont des fichiers cochés
    std::function<void(const QModelIndex&, const QString&)> identifySelectedChapters = 
    [&](const QModelIndex& parent, const QString& currentChapter) {
        for (int i = 0; i < model->rowCount(parent); ++i) {
            QModelIndex index = model->index(i, 0, parent);
            Qt::CheckState state = static_cast<Qt::CheckState>(model->data(index, Qt::CheckStateRole).toInt());
            
            QString nodeName = model->data(index, Qt::DisplayRole).toString();
            
            // Si c'est un nœud de premier niveau (chapitre)
            if (parent == QModelIndex() && nodeName != "PEDA" && nodeName != "DOCS" && nodeName != "EVALS") {
                // Passer en paramètre le nom du chapitre
                identifySelectedChapters(index, nodeName);
            }
            else if (parent != QModelIndex()) {
                // Si c'est un fichier coché (pas un dossier)
                if (state == Qt::Checked && !currentChapter.isEmpty()) {
                    // Marquer ce chapitre pour compilation
                    chaptersToCompile.insert(currentChapter);
                    qDebug() << "Chapitre marqué pour compilation:" << currentChapter;
                }
                
                // Explorer récursivement
                if (model->hasChildren(index)) {
                    identifySelectedChapters(index, currentChapter);
                }
            }
        }
    };
    
    // Seconde passe : collecter TOUS les fichiers des chapitres à compiler
    std::function<void(const QModelIndex&)> collectAllChapterFiles = 
    [&](const QModelIndex& parent) {
        for (int i = 0; i < model->rowCount(parent); ++i) {
            QModelIndex index = model->index(i, 0, parent);
            QString nodeName = model->data(index, Qt::DisplayRole).toString();
            QString nodePath = model->data(index, Qt::ToolTipRole).toString();
            
            // Si c'est un nœud de premier niveau (chapitre)
            if (parent == QModelIndex()) {
                if (nodeName != "PEDA" && nodeName != "DOCS" && nodeName != "EVALS") {
                    // Si ce chapitre doit être compilé
                    if (chaptersToCompile.contains(nodeName)) {
                        // Créer une entrée pour ce chapitre
                        ChapterInfo chapter;
                        chapter.name = nodeName;
                        chapter.path = nodePath;
                        chapterMap[nodeName] = chapter;
                        
                        // Collecter tous les fichiers du chapitre
                        collectAllChapterFiles(index);
                    }
                }
            }
            else {
                // Déterminer à quel chapitre ce nœud appartient
                QModelIndex parentIdx = parent;
                QString chapterName;
                
                // Remonter jusqu'à trouver un nœud de premier niveau
                while (parentIdx.parent().isValid()) {
                    parentIdx = parentIdx.parent();
                }
                
                // Si on est remonté à un chapitre
                if (!parentIdx.parent().isValid()) {
                    chapterName = model->data(parentIdx, Qt::DisplayRole).toString();
                    
                    // Pour chaque fichier sélectionné
                    QFileInfo fileInfo(nodePath);
                    QString fileName = fileInfo.fileName();
                    
                    // Exclure UNIQUEMENT les fichiers qui se nomment exactement {chapitre}_cours.tex
                    // mais continuer à explorer récursivement leurs enfants
                    bool isCoursFile = fileName.contains("_cours");
                    if (isCoursFile) {
                        qDebug() << "Fichier de cours exclu:" << nodePath;
                        
                        // Explorer récursivement les enfants même si on a exclu le fichier cours
                        if (model->hasChildren(index)) {
                            collectAllChapterFiles(index);
                        }
                        
                        continue;  // Ne pas ajouter ce fichier spécifique
                    }
                    
                    // Si ce chapitre doit être compilé et que c'est un fichier .tex
                    if (chaptersToCompile.contains(chapterName) && 
                        !nodePath.isEmpty() && QFile::exists(nodePath) && nodePath.endsWith(".tex")) {
                        
                        // Ajouter ce fichier au chapitre
                        chapterMap[chapterName].files.append(qMakePair(nodeName, nodePath));
                        qDebug() << "Ajout du fichier" << nodePath << "au chapitre" << chapterName;
                    }
                }
                
                // Explorer récursivement - On laisse ce code ici pour ne pas perturber la logique existante
                // qui gère d'autres cas comme les nœuds sans fichiers
                if (model->hasChildren(index)) {
                    collectAllChapterFiles(index);
                }
            }
        }
    };
    
    // Exécuter les deux passes
    identifySelectedChapters(QModelIndex(), "");
    collectAllChapterFiles(QModelIndex());
    
    // Convertir la map en vecteur
    for (const auto& chapterName : chapterMap.keys()) {
        if (!chapterMap[chapterName].files.isEmpty()) {
            chapters.append(chapterMap[chapterName]);
            qDebug() << "Chapitre" << chapterName << "ajouté avec" << chapterMap[chapterName].files.size() << "fichiers";
        }
    }
    
    qDebug() << "Chapitres à compiler trouvés:" << chapters.size();
    
    return chapters;
}

QString LatexAssembler::createChapterTempFile(const QString& preamble, const ChapterInfo& chapter)
{
    // Vérifier que le répertoire temporaire pour les chapitres existe
    if (!s_chapterTempDir.isValid()) {
        emit compilationError("Impossible de créer un répertoire temporaire pour les chapitres");
        return QString();
    }
    
    // Générer un nom unique pour le fichier temporaire
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    QString tempFileName = s_chapterTempDir.path() + "/temp_" + chapter.name + "_" + timestamp + ".tex";
    
    qDebug() << "Création du fichier temporaire pour chapitre:" << chapter.name << "avec" << chapter.files.size() << "fichiers";
    
    QFile tempFile(tempFileName);
    if (!tempFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit compilationError("Impossible de créer le fichier temporaire pour le chapitre " + chapter.name);
        return QString();
    }
    
    QTextStream out(&tempFile);
    
    // Écrire le préambule
    out << preamble;
    
    // Obtenir le chemin absolu du dossier contenant le fichier principal
    QFileInfo mainFileInfo(m_mainFilePath);
    QString mainDirPath = mainFileInfo.absolutePath();
    
    // Ajouter la configuration des chemins d'images
    out << "% Configuration des chemins d'images pour le document temporaire\n";
    out << "\\graphicspath{{"
        << QDir::toNativeSeparators(mainDirPath + "/images/").replace("\\", "/") << "}{"
        << QDir::toNativeSeparators(mainDirPath + "/../images/").replace("\\", "/") << "}}\n\n";
    
    // Ajouter la configuration des en-têtes et pieds de page
    out << "% Configuration des en-têtes et pieds de page\n";
    out << "\\lhead{\\textcolor{gris50}{\\small\\textit{\\hyperlink{debut}{\\monetablissement}}}} %haut de page gauche\n";
    out << "\\chead{} %haut de page centre\n";
    out << "\\rhead{\\textcolor{gris50}{\\small\\textit{\\hyperlink{debut}{\\maclasse}}}} %haut de page droit\n";
    out << "\\lfoot{} %pied de page gauche\n";
    out << "\\cfoot{\\textcolor{gris50}{\\small\\textit{page \\thepage}}} % pied de page centré\n";
    out << "\\rfoot{} %On personnalisera cette en-tête\n";
    out << "\\def\\headrulewidth{0pt} %Trace un trait de séparation de largeur 0,4 point. Mettre 0pt pour supprimer le trait.\n";
    out << "\\def\\footrulewidth{0pt} %Trace un trait de séparation de largeur 0,4 point. Mettre 0pt pour supprimer le trait.\n";
    out << "\\pagestyle{empty}\n\n";
    
    // Début du document
    out << "\\begin{document}\n\n";
    
    // Ajouter la configuration de l'espacement
    out << "\\singlespacing\\setlength{\\parindent}{0pt}\n\n";
    
    // Ajouter le contenu des fichiers du chapitre
    for (const auto& file : chapter.files) {
        QFile inputFile(file.second);
        if (inputFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            out << "% Contenu du fichier: " << file.second << "\n";
            out << inputFile.readAll() << "\n\n";
            inputFile.close();
        }
    }
    
    // Fin du document
    out << "\\end{document}\n";
    
    tempFile.close();
    
    qDebug() << "Fichier temporaire créé:" << tempFileName;
    
    // Ajouter à la liste des fichiers temporaires de chapitres
    m_chapterTempFiles.append(tempFileName);
    
    return tempFileName;
}

void LatexAssembler::compileChapters(LatexModel* model, QTextEdit* outputWidget)
{
    // Utiliser une variable de comptage de compilation SÉPARÉE pour les chapitres
    m_chapterCompilationCount = 1; // Nouveau membre à ajouter au header
    
    // Nettoyer la file d'attente existante
    {
        QMutexLocker locker(&m_mutex);
        m_chapterQueue.clear();
    }
    
    // Configurer la sortie avant tout
    m_chapterOutputWidget = outputWidget;
    m_chapterOutputWidget->clear();
    m_chapterOutputWidget->append("=== DÉBUT DE LA COMPILATION DES CHAPITRES ===\n");
    
    // Identifier les chapitres à compiler
    QVector<ChapterInfo> chapters = identifyChaptersToCompile(model);
    qDebug() << "Chapitres identifiés:" << chapters.size();
    
    if (chapters.isEmpty()) {
        m_chapterOutputWidget->append("Aucun chapitre à compiler. Vérifiez qu'au moins un fichier est coché.");
        emit compilationError("Aucun chapitre à compiler");
        return;
    }
    
    // Démarrer le processus de compilation
    {
        QMutexLocker locker(&m_mutex);
        // Ajouter les chapitres à la file d'attente
        for (const ChapterInfo& chapter : chapters) {
            m_chapterQueue.enqueue(chapter);
        }
    }
    
    m_isCompilingChapters = true;
    
    // Démarrer le processus de manière asynchrone
    QMetaObject::invokeMethod(this, "processNextChapter", Qt::QueuedConnection);
}

void LatexAssembler::processNextChapter()
{
    // Récupérer le prochain chapitre
    ChapterInfo chapter;
    bool hasChapter = false;
    
    {
        QMutexLocker locker(&m_mutex);
        if (!m_chapterQueue.isEmpty()) {
            chapter = m_chapterQueue.dequeue();
            hasChapter = true;
        } else {
            m_isCompilingChapters = false;
            QMetaObject::invokeMethod(this, "allChaptersCompiled", Qt::QueuedConnection);
            return;
        }
    }
    
    if (!hasChapter) return;
    
    m_currentChapterName = chapter.name;
    qDebug() << "Traitement du chapitre:" << m_currentChapterName;
    
    // Afficher un séparateur pour ce chapitre
    if (m_chapterOutputWidget) {
        m_chapterOutputWidget->append("\n\n*******************************************************");
        m_chapterOutputWidget->append(QString("***********   CHAPITRE : %1       **************").arg(chapter.name));
        m_chapterOutputWidget->append("*******************************************************\n\n");
    }
    
    emit chapterCompilationStarted(chapter.name);
    
    // Extraire le préambule du document principal
    QString preamble = extractPreamble(m_mainFilePath);
    if (preamble.isEmpty()) {
        emit compilationError("Impossible de lire le préambule pour le chapitre " + chapter.name);
        QMetaObject::invokeMethod(this, "processNextChapter", Qt::QueuedConnection);
        return;
    }
    
    // Créer le fichier temporaire pour ce chapitre
    QString tempFilePath = createChapterTempFile(preamble, chapter);
    if (tempFilePath.isEmpty()) {
        QMetaObject::invokeMethod(this, "processNextChapter", Qt::QueuedConnection);
        return;
    }
    
    // Compiler ce chapitre
    m_currentChapterTempFile = tempFilePath;  // Utiliser la variable dédiée aux chapitres
    m_chapterCompilationCount = 1;
    
    QFileInfo tempFileInfo(tempFilePath);
    QStringList args;
    args << "-synctex=1"
         << "-shell-escape"
         << "-interaction=nonstopmode"
         << "-file-line-error"
         << tempFilePath;
    
    // En cas d'erreur au lancement
    if (!m_chapterProcessRunner->runCommand("lualatex", args, m_chapterOutputWidget, tempFileInfo.absolutePath())) {
        emit compilationError("Échec du lancement de la compilation pour le chapitre " + chapter.name);
        QMetaObject::invokeMethod(this, "processNextChapter", Qt::QueuedConnection);
    }
}

QString LatexAssembler::renameChapterPdf(const QString& tempFilePath, const QString& chapterName)
{
    // Obtenir les informations sur les fichiers
    QFileInfo tempFileInfo(tempFilePath);
    QFileInfo mainFileInfo(m_mainFilePath);
    
    if (!mainFileInfo.exists()) {
        qDebug() << "Fichier principal non trouvé pour renommer le PDF du chapitre";
        return QString();
    }
    
    // Nettoyer le nom du chapitre
    QString cleanChapterName = chapterName;
    cleanChapterName.replace("_cours", "");
    cleanChapterName.replace(" ", "_");
    
    // Récupérer le nom de fichier principal (sans extension)
    QString baseName = mainFileInfo.completeBaseName();
    
    // Générer un timestamp formaté
    QString timestamp = QDateTime::currentDateTime().toString("yyyy_MM_dd_HH_mm_ss");
    
    // Chemin du nouveau fichier PDF dans le même répertoire que le document principal
    QString newPdfName = mainFileInfo.absolutePath() + "/" + baseName + "_" + cleanChapterName + 
                       "_" + timestamp + ".pdf";
    
    // Chemin du PDF original généré
    QString originalPdf = tempFileInfo.absolutePath() + "/" + tempFileInfo.completeBaseName() + ".pdf";
    
    qDebug() << "Renommage du PDF du chapitre:" << originalPdf << "->" << newPdfName;
    
    if (QFile::exists(originalPdf)) {
        // Copier le fichier vers sa destination finale
        if (QFile::copy(originalPdf, newPdfName)) {
            // Supprimer l'original si la copie a réussi
            QFile::remove(originalPdf);
            qDebug() << "PDF du chapitre renommé avec succès";
            return newPdfName;
        } else {
            qDebug() << "Échec de la copie du PDF du chapitre";
        }
    } else {
        qDebug() << "PDF original non trouvé:" << originalPdf;
    }
    
    return QString();
}

void LatexAssembler::compileFullDocument(LatexModel* model, QTextEdit* outputWidget)
{
    // Utiliser une variable de comptage de compilation SÉPARÉE
    m_fullDocumentCompilationCount = 1;
    
    // Configurer la sortie
    m_fullDocumentOutputWidget = outputWidget;
    m_fullDocumentOutputWidget->clear();
    m_fullDocumentOutputWidget->append("=== DÉBUT DE LA COMPILATION DU DOCUMENT COMPLET ===\n");
    
    // Collecter tous les fichiers du document
    QVector<QPair<QString, QString>> documentFiles = collectAllDocumentFiles(model);
    
    if (documentFiles.isEmpty()) {
        m_fullDocumentOutputWidget->append("Aucun fichier à compiler pour le document complet.");
        emit compilationError("Aucun fichier à compiler pour le document complet");
        return;
    }
    
    // Extraire le préambule du document principal
    QString preamble = extractPreamble(m_mainFilePath);
    if (preamble.isEmpty()) {
        emit compilationError("Impossible de lire le préambule pour le document complet");
        return;
    }
    
    // Créer le fichier temporaire pour le document complet
    QString tempFilePath = createFullDocumentTempFile(preamble, documentFiles);
    if (tempFilePath.isEmpty()) {
        return;
    }
    
    // Démarrer la compilation
    m_fullDocumentTempFile = tempFilePath;
    m_fullDocumentCompilationCount = 1;
    m_isCompilingFullDocument = true;
    
    emit fullDocumentCompilationStarted();
    
    QFileInfo tempFileInfo(tempFilePath);
    QStringList args;
    args << "-synctex=1"
         << "-shell-escape"
         << "-interaction=nonstopmode"
         << "-file-line-error"
         << tempFilePath;
    
    // Lancer la compilation
    if (!m_fullDocumentProcessRunner->runCommand("lualatex", args, m_fullDocumentOutputWidget, tempFileInfo.absolutePath())) {
        emit compilationError("Échec du lancement de la compilation du document complet");
        m_isCompilingFullDocument = false;
    }
}

QVector<QPair<QString, QString>> LatexAssembler::collectAllDocumentFiles(LatexModel* model)
{
    QVector<QPair<QString, QString>> documentFiles;
    
    // Fonction récursive pour parcourir tout le modèle en une seule passe
    std::function<void(const QModelIndex&)> collectFiles = [&](const QModelIndex& parent) {
        for (int i = 0; i < model->rowCount(parent); ++i) {
            QModelIndex index = model->index(i, 0, parent);
            QString nodeName = model->data(index, Qt::DisplayRole).toString();
            QString nodePath = model->data(index, Qt::ToolTipRole).toString();
            
            qDebug() << "Examen du nœud:" << nodeName << "Chemin:" << nodePath;
            
            // Si c'est un nœud de premier niveau qui est un dossier spécial, l'ignorer complètement
            if (parent == QModelIndex() && (nodeName == "PEDA" || nodeName == "DOCS" || nodeName == "EVALS")) {
                continue; // On ignore complètement ces dossiers racine
            }
            
            // Si c'est un dossier spécial au sein d'un chapitre, explorer pour trouver les fichiers non-_cours
            if (nodeName == "PEDA" || nodeName == "DOCS" || nodeName == "EVALS") {
                qDebug() << "👉 Explorer le contenu du dossier spécial:" << nodeName;
                // On continue à explorer le dossier, mais on ne l'ajoute pas lui-même
                collectFiles(index);
                continue;
            }
            
            // Si c'est un fichier .tex valide
            if (!nodePath.isEmpty() && QFile::exists(nodePath) && nodePath.endsWith(".tex")) {
                // Ne pas ajouter les fichiers _cours
                if (nodePath.contains("_cours")) {
                    qDebug() << "❌ Fichier _cours exclu:" << nodePath;
                    
                    // Explorer récursivement les enfants du _cours pour ne pas les perdre
                    if (model->hasChildren(index)) {
                        collectFiles(index);
                    }
                } else {
                    // IMPORTANT: Pour tous les autres fichiers .tex, les ajouter à la collection
                    documentFiles.append(qMakePair(nodeName, nodePath));
                    qDebug() << "✅ Ajout du fichier" << nodePath << "au document complet";
                }
            } 
            // Si ce n'est pas un fichier .tex ou s'il n'existe pas, c'est peut-être un dossier
            else if (model->hasChildren(index)) {
                qDebug() << "👉 Explorer les enfants de:" << nodeName;
                collectFiles(index);
            }
        }
    };
    
    // Démarrer la collecte depuis la racine
    collectFiles(QModelIndex());
    
    qDebug() << "=== RÉSUMÉ DES FICHIERS COLLECTÉS ===";
    qDebug() << "Total des fichiers collectés pour le document complet:" << documentFiles.size();
    for (const auto& file : documentFiles) {
        qDebug() << "Fichier à inclure:" << file.first << "-" << file.second;
    }
    
    return documentFiles;
}

QString LatexAssembler::createFullDocumentTempFile(const QString& preamble, const QVector<QPair<QString, QString>>& files)
{
    if (!s_documentTempDir.isValid()) {
        emit compilationError("Impossible de créer un répertoire temporaire pour le document complet");
        return QString();
    }
    
    // Générer un nom unique pour le fichier temporaire
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    QString tempFileName = s_documentTempDir.path() + "/temp_full_document_" + timestamp + ".tex";
    
    QFile tempFile(tempFileName);
    if (!tempFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit compilationError("Impossible de créer le fichier temporaire pour le document complet");
        return QString();
    }
    
    QTextStream out(&tempFile);
    
    // Écrire le préambule
    out << preamble;
    
    // Obtenir le chemin absolu du dossier contenant le fichier principal
    QFileInfo mainFileInfo(m_mainFilePath);
    QString mainDirPath = mainFileInfo.absolutePath();
    
    // Ajouter la configuration des chemins d'images
    out << "% Configuration des chemins d'images pour le document temporaire\n";
    out << "\\graphicspath{{"
        << QDir::toNativeSeparators(mainDirPath + "/images/").replace("\\", "/") << "}{"
        << QDir::toNativeSeparators(mainDirPath + "/../images/").replace("\\", "/") << "}}\n\n";
    
    // Ajouter la configuration des en-têtes et pieds de page
    out << "% Configuration des en-têtes et pieds de page\n";
    out << "\\lhead{\\textcolor{gris50}{\\small\\textit{\\hyperlink{debut}{\\monetablissement}}}} %haut de page gauche\n";
    out << "\\chead{} %haut de page centre\n";
    out << "\\rhead{\\textcolor{gris50}{\\small\\textit{\\hyperlink{debut}{\\maclasse}}}} %haut de page droit\n";
    out << "\\lfoot{} %pied de page gauche\n";
    out << "\\cfoot{\\textcolor{gris50}{\\small\\textit{page \\thepage}}} % pied de page centré\n";
    out << "\\rfoot{} %On personnalisera cette en-tête\n";
    out << "\\def\\headrulewidth{0pt} %Trace un trait de séparation de largeur 0,4 point. Mettre 0pt pour supprimer le trait.\n";
    out << "\\def\\footrulewidth{0pt} %Trace un trait de séparation de largeur 0,4 point. Mettre 0pt pour supprimer le trait.\n";
    out << "\\pagestyle{empty}\n\n";
    
    // Début du document
    out << "\\begin{document}\n\n";
    
    // Ajouter la configuration de l'espacement
    out << "\\singlespacing\\setlength{\\parindent}{0pt}\n\n";
    
    // Vérifier que nous avons des fichiers à inclure
    if (files.isEmpty()) {
        qDebug() << "ATTENTION: Aucun fichier à inclure dans le document complet!";
    }
    
    // Ajouter le contenu des fichiers sélectionnés
    int filesProcessed = 0;
    for (const auto& file : files) {
        QFile inputFile(file.second);
        if (inputFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            out << "% Contenu du fichier: " << file.second << "\n";
            QString content = inputFile.readAll();
            out << content << "\n\n";
            inputFile.close();
            filesProcessed++;
            qDebug() << "✓ Inclusion réussie du fichier:" << file.second;
        } else {
            qDebug() << "⚠️ Impossible d'ouvrir le fichier:" << file.second;
        }
    }
    
    qDebug() << "Fichiers traités:" << filesProcessed << "sur" << files.size() << "attendus";
    
    // Fin du document
    out << "\\end{document}\n";
    
    tempFile.close();
    
    qDebug() << "Fichier temporaire du document complet créé:" << tempFileName;
    
    // Ajouter à la liste pour le nettoyage des fichiers temporaires de document
    m_documentTempFiles.append(tempFileName);
    
    return tempFileName;
}

QString LatexAssembler::renameFullDocumentPdf(const QString& tempFilePath)
{
    // Obtenir les informations sur les fichiers
    QFileInfo tempFileInfo(tempFilePath);
    QFileInfo mainFileInfo(m_mainFilePath);
    
    // S'assurer qu'on utilise le vrai chemin du document principal
    if (m_mainFilePath.isEmpty() || !mainFileInfo.exists()) {
        return QString(); // Impossible de renommer sans chemin valide
    }
    
    // Récupérer le nom de fichier principal (sans extension)
    QString baseName = mainFileInfo.completeBaseName();
    
    // Générer un timestamp formaté
    QString timestamp = QDateTime::currentDateTime().toString("yyyy_MM_dd_HH_mm_ss");
    
    // Chemin du nouveau fichier PDF dans le même répertoire que le document principal
    QString newPdfName = mainFileInfo.absolutePath() + "/" + baseName + "_complet_" + timestamp + ".pdf";
    
    // Chemin du PDF original généré
    QString originalPdf = tempFileInfo.absolutePath() + "/" + tempFileInfo.completeBaseName() + ".pdf";
    
    if (QFile::exists(originalPdf)) {
        // Copier le fichier vers sa destination finale
        if (QFile::copy(originalPdf, newPdfName)) {
            // Supprimer l'original si la copie a réussi
            QFile::remove(originalPdf);
            
            // Stocker le chemin du dernier PDF généré
            m_lastPdfPath = newPdfName;
            
            return newPdfName;
        }
    }
    
    return QString(); // Retourner une chaîne vide en cas d'échec
}

void LatexAssembler::processFullDocument()
{
    // Cette méthode est appelée lorsqu'un document complet est prêt à être traité
    // après avoir été mis en file d'attente
    
    if (m_isCompilingFullDocument) {
        qDebug() << "Déjà en train de compiler un document complet, ignoré";
        return;
    }
    
    // Démarrer la compilation du document complet
    m_fullDocumentCompilationCount = 1;
    m_isCompilingFullDocument = true;
    
    // Afficher un message de début dans le widget de sortie
    if (m_fullDocumentOutputWidget) {
        m_fullDocumentOutputWidget->append("=== COMPILATION DU DOCUMENT COMPLET ===\n");
    }
    
    emit fullDocumentCompilationStarted();
    emit fullDocumentCompilationProgress(m_fullDocumentCompilationCount, 5);
    
    // Lancer le processus de compilation avec lualatex
    QFileInfo tempFileInfo(m_fullDocumentTempFile);
    QStringList args;
    args << "-synctex=1"
         << "-shell-escape"
         << "-interaction=nonstopmode"
         << "-file-line-error"
         << m_fullDocumentTempFile;
    
    if (!m_fullDocumentProcessRunner->runCommand("lualatex", args, m_fullDocumentOutputWidget, tempFileInfo.absolutePath())) {
        emit compilationError("Échec du lancement de la compilation du document complet");
        m_isCompilingFullDocument = false;
    }
}