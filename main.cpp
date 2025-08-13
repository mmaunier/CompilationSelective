#include <QApplication>
#include <QTreeView>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QMainWindow>
#include <QFileInfo>
#include <QIcon>
#include <QTimer>
#include <QCheckBox>
#include <QTabWidget>
#include <QTextEdit>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSplitter>
#include <QDesktopServices>
#include <QUrl>
#include <QStyleFactory>
#include "latexmodel.h"
#include "lastfilehelper.h"
#include "processrunner.h"
#include "latexassembler.h"

// Forward declaration - ajoutée pour résoudre l'erreur de compilation
bool hasCheckedNodesRecursive(QAbstractItemModel* model, const QModelIndex& parent);

// Fonction auxiliaire pour développer seulement les nœuds cochés ou partiellement cochés
void expandCheckedNodes(QTreeView* treeView, const QModelIndex& parent = QModelIndex())
{
    QAbstractItemModel* model = treeView->model();
    if (!model) return;
    
    int rows = model->rowCount(parent);
    for (int i = 0; i < rows; ++i) {
        QModelIndex index = model->index(i, 0, parent);
        
        // Vérifier l'état de la case à cocher
        QVariant checkState = model->data(index, Qt::CheckStateRole);
        bool checked = checkState.isValid() && 
                       (checkState.toInt() == Qt::Checked || checkState.toInt() == Qt::PartiallyChecked);
        
        // Si le nœud est coché/partiellement coché, le développer
        if (checked) {
            treeView->expand(index);
        }
        
        // Vérifier récursivement les enfants (qu'ils soient développés ou non)
        // pour assurer que les parents des nœuds cochés sont aussi développés
        if (model->hasChildren(index)) {
            bool hasCheckedChildren = hasCheckedNodesRecursive(model, index);
            if (hasCheckedChildren) {
                treeView->expand(index);
            }
            expandCheckedNodes(treeView, index);
        }
    }
}

// Fonction auxiliaire pour vérifier si un nœud a des descendants cochés
bool hasCheckedNodesRecursive(QAbstractItemModel* model, const QModelIndex& parent)
{
    int rows = model->rowCount(parent);
    for (int i = 0; i < rows; ++i) {
        QModelIndex index = model->index(i, 0, parent);
        
        // Vérifier l'état de la case à cocher
        QVariant checkState = model->data(index, Qt::CheckStateRole);
        if (checkState.isValid() && 
            (checkState.toInt() == Qt::Checked || checkState.toInt() == Qt::PartiallyChecked)) {
            return true;
        }
        
        // Vérifier récursivement les enfants
        if (model->hasChildren(index) && hasCheckedNodesRecursive(model, index)) {
            return true;
        }
    }
    return false;
}

int main(int argc, char *argv[])
{

    QApplication app(argc, argv);
    
    // Définir l'icône de l'application
    QIcon appIcon;
    appIcon.addFile(":/images/logo_16.png", QSize(16, 16));
    appIcon.addFile(":/images/logo_32.png", QSize(32, 32));
    appIcon.addFile(":/images/logo_48.png", QSize(48, 48));
    appIcon.addFile(":/images/logo_64.png", QSize(64, 64));
    appIcon.addFile(":/images/logo_128.png", QSize(128, 128));
    appIcon.addFile(":/images/logo.png", QSize(256, 256));
    app.setWindowIcon(appIcon);

    // Crée le modèle et charge le dernier fichier
    LatexModel model;
    LastFileHelper lastFileHelper;
    QString lastFile = lastFileHelper.loadLastFilePath();

    // Crée la fenêtre principale
    QMainWindow window;
    window.setWindowTitle("Compilation LaTeX Sélective");
    window.setWindowIcon(appIcon);
    window.resize(1600, 800);

    // Widget central avec layout horizontal pour diviser la fenêtre
    QWidget* centralWidget = new QWidget(&window);
    QHBoxLayout* mainLayout = new QHBoxLayout(centralWidget);

    // ----------- PARTIE GAUCHE (EXISTANTE) -----------
    QWidget* leftWidget = new QWidget(centralWidget);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftWidget);
    
    // Ligne pour le chemin du fichier et bouton de sélection
    QHBoxLayout* filePathLayout = new QHBoxLayout();
    QLineEdit* filePathEdit = new QLineEdit(lastFile, leftWidget);
    filePathEdit->setPlaceholderText("Chemin du fichier LaTeX...");
    filePathLayout->addWidget(filePathEdit, 1);
    
    QPushButton* browseButton = new QPushButton(leftWidget);
    browseButton->setIcon(QIcon::fromTheme("folder-open"));
    browseButton->setToolTip("Parcourir...");
    browseButton->setFixedSize(30, filePathEdit->sizeHint().height());
    filePathLayout->addWidget(browseButton);
    leftLayout->addLayout(filePathLayout);
    
    // Bouton pour charger le fichier
    QPushButton* loadButton = new QPushButton("Charger le fichier LaTeX", leftWidget);
    leftLayout->addWidget(loadButton);
    
    // TreeView pour afficher l'arborescence
    QTreeView* treeView = new QTreeView(leftWidget);
    treeView->setModel(&model);
    treeView->setAlternatingRowColors(true);
    treeView->header()->hide();
    leftLayout->addWidget(treeView);
    
    // Bouton de compilation existant
    QPushButton* compileButton = new QPushButton("Compilation LuaLaTeX ...", leftWidget);
    QFont buttonFont("Noto Sans", 20);
    buttonFont.setBold(true);
    buttonFont.setItalic(true);
    compileButton->setFont(buttonFont);
    
    // Style du bouton
    QPalette palette = compileButton->palette();
    palette.setColor(QPalette::ButtonText, QColor(100, 50, 150));
    compileButton->setPalette(palette);
    compileButton->setMinimumHeight(50);
    compileButton->setCursor(Qt::PointingHandCursor);
    
    leftLayout->addSpacing(10);
    leftLayout->addWidget(compileButton);
    
    // ----------- PARTIE DROITE (MODIFIÉE) -----------
    QWidget* rightWidget = new QWidget(centralWidget);
    QVBoxLayout* rightLayout = new QVBoxLayout(rightWidget);

    // Charger les options de compilation sauvegardées
    auto [savedCompileChapter, savedCompileDocument] = lastFileHelper.loadCompilationOptions();

    // Widget pour contenir les options sur une seule ligne
    QHBoxLayout* optionsLayout = new QHBoxLayout();

    // Options de compilation (2 checkboxes)
    QCheckBox* compileChapterCheckbox = new QCheckBox("Recompiler tout le chapitre", rightWidget);
    compileChapterCheckbox->setChecked(savedCompileChapter); // Coché par défaut
    optionsLayout->addWidget(compileChapterCheckbox);

    QCheckBox* compileDocumentCheckbox = new QCheckBox("Recompiler tout le document", rightWidget);
    compileDocumentCheckbox->setChecked(savedCompileDocument); // Non coché par défaut
    optionsLayout->addWidget(compileDocumentCheckbox);

    // Bouton pour ouvrir le PDF (désactivé par défaut)
    QPushButton* openPdfButton = new QPushButton("Ouvrir PDF", rightWidget);
    openPdfButton->setEnabled(false);
    openPdfButton->setIcon(QIcon::fromTheme("document-open"));
    optionsLayout->addWidget(openPdfButton);

    // Bouton pour annuler la compilation
    QPushButton* cancelButton = new QPushButton("Annuler", rightWidget);
    cancelButton->setIcon(QIcon::fromTheme("process-stop"));
    cancelButton->setEnabled(false); // Désactivé au démarrage
    optionsLayout->addWidget(cancelButton);

    // Ajouter les options au layout
    rightLayout->addLayout(optionsLayout);
    rightLayout->addSpacing(10);

    // TabWidget pour les sorties de compilation
    QTabWidget* outputTabWidget = new QTabWidget(rightWidget);
    
    // Créer les 3 onglets avec des QTextEdit pour afficher les sorties
    QTextEdit* partialOutputText = new QTextEdit(outputTabWidget);
    partialOutputText->setReadOnly(true);
    partialOutputText->setFont(QFont("Monospace"));
    partialOutputText->setPlaceholderText("La sortie de compilation partielle apparaîtra ici...");
    
    QTextEdit* chapterOutputText = new QTextEdit(outputTabWidget);
    chapterOutputText->setReadOnly(true);
    chapterOutputText->setFont(QFont("Monospace"));
    chapterOutputText->setPlaceholderText("La sortie de compilation du chapitre apparaîtra ici...");
    
    QTextEdit* documentOutputText = new QTextEdit(outputTabWidget);
    documentOutputText->setReadOnly(true);
    documentOutputText->setFont(QFont("Monospace"));
    documentOutputText->setPlaceholderText("La sortie de compilation du document complet apparaîtra ici...");
    
    // Ajouter les onglets au TabWidget
    outputTabWidget->addTab(partialOutputText, "Partiel");
    outputTabWidget->addTab(chapterOutputText, "Chapitre");
    outputTabWidget->addTab(documentOutputText, "Document");
    
    // Ajouter le TabWidget au layout droit, il prendra tout l'espace disponible
    rightLayout->addWidget(outputTabWidget);
    
    // ----------- ASSEMBLAGE DES DEUX PARTIES -----------
    mainLayout->addWidget(leftWidget);
    mainLayout->addSpacing(20);
    mainLayout->addWidget(rightWidget);
    
    // Équilibrer les proportions des deux parties
    mainLayout->setStretchFactor(leftWidget, 1);
    mainLayout->setStretchFactor(rightWidget, 1);

    // Créer notre assembleur LaTeX
    LatexAssembler *latexAssembler = new LatexAssembler(&window);

    // Fonction de sauvegarde des options
    auto saveOptions = [&]() {
        lastFileHelper.saveCompilationOptions(
            compileChapterCheckbox->isChecked(),
            compileDocumentCheckbox->isChecked()
        );
    };
    
    // Connecter les checkboxes pour sauvegarder les changements
    QObject::connect(compileChapterCheckbox, &QCheckBox::toggled, [&]() {
        lastFileHelper.saveCompilationOptions(
            compileChapterCheckbox->isChecked(),
            compileDocumentCheckbox->isChecked()
        );
    });
    QObject::connect(compileDocumentCheckbox, &QCheckBox::toggled, [&]() {
        lastFileHelper.saveCompilationOptions(
            compileChapterCheckbox->isChecked(),
            compileDocumentCheckbox->isChecked()
        );
    });
    
    // Variable pour stocker le chemin du dernier PDF
    QString lastPdfPath;

    // Connexion pour ouvrir le PDF
    QObject::connect(openPdfButton, &QPushButton::clicked, [&lastPdfPath]() {
        if (!lastPdfPath.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(lastPdfPath));
        }
    });

    // Connexion pour le bouton d'annulation
    QObject::connect(cancelButton, &QPushButton::clicked, [&]() {
        latexAssembler->stopCompilation();
        compileButton->setEnabled(true);
        cancelButton->setEnabled(false);
    });

    // Fonction de chargement de fichier modifiée
    auto loadFile = [&](const QString& filePath) {
        if (!filePath.isEmpty() && QFileInfo(filePath).exists() && filePath.endsWith(".tex")) {
            // Sauvegarder l'état actuel
            if (model.hasData()) {
                QJsonObject currentState = model.saveCheckState();
                lastFileHelper.saveCheckState(currentState);
            }
            
            // Vérifier si c'est un nouveau fichier (différent du dernier chargé)
            bool isNewFile = (filePath != lastFile && !lastFile.isEmpty());
            
            if (isNewFile) {
                // Réinitialiser les options à leurs valeurs par défaut
                compileChapterCheckbox->setChecked(true);
                compileDocumentCheckbox->setChecked(false);
                
                // Vider les sorties
                partialOutputText->clear();
                chapterOutputText->clear();
                documentOutputText->clear();
                
                // Sauvegarder ces options par défaut
                saveOptions();
            }
            
            // Vider les sorties
            partialOutputText->clear();
            chapterOutputText->clear();
            documentOutputText->clear();
            
            // Désactiver le bouton "Ouvrir PDF" pour un nouveau fichier
            openPdfButton->setEnabled(false);
            lastPdfPath.clear();
            
            filePathEdit->setText(filePath);
            model.loadFromFile(filePath);
            
            // Restaurer l'état précédent
            QJsonObject savedState = lastFileHelper.loadCheckState();
            if (!savedState.isEmpty()) {
                model.restoreCheckState(savedState);
            }
            
            treeView->collapseAll(); 
            expandCheckedNodes(treeView);
            lastFileHelper.saveLastFilePath(filePath);
        }
    };
    
    // Charger le dernier fichier
    if (!lastFile.isEmpty()) {
        loadFile(lastFile);
    }
    
    // Connecter les signaux aux slots
    QObject::connect(browseButton, &QPushButton::clicked, [&]() {
        QString filePath = QFileDialog::getOpenFileName(
            &window, "Ouvrir un fichier LaTeX", 
            filePathEdit->text().isEmpty() ? "" : QFileInfo(filePathEdit->text()).absolutePath(),
            "Fichiers LaTeX (*.tex)");
        
        if (!filePath.isEmpty()) {
            loadFile(filePath);
        }
    });
    
    QObject::connect(loadButton, &QPushButton::clicked, [&]() {
        loadFile(filePathEdit->text());
    });
    
    QObject::connect(filePathEdit, &QLineEdit::returnPressed, [&]() {
        loadFile(filePathEdit->text());
    });
    
    // Connecter le bouton de compilation
    QObject::connect(compileButton, &QPushButton::clicked, [&]() {
        // Désactiver le bouton pendant la compilation
        compileButton->setEnabled(false);
        cancelButton->setEnabled(true); // Activer le bouton d'annulation
        
        // Désactiver le bouton "Ouvrir PDF" pendant la nouvelle compilation
        openPdfButton->setEnabled(false);
        
        // Arrêter toute compilation en cours
        latexAssembler->stopCompilation();
        
        // Vider les onglets de sortie
        partialOutputText->clear();
        chapterOutputText->clear();
        documentOutputText->clear();
        
        // Créer le document partiel
        QString tempFilePath = latexAssembler->createPartialDocument(filePathEdit->text(), &model);
        
        if (!tempFilePath.isEmpty()) {
            // Toujours utiliser l'onglet "Partiel" pour la sortie partielle
            outputTabWidget->setCurrentIndex(0);
            
            // Lancer la compilation partielle
            latexAssembler->compilePartialDocument(
                tempFilePath,
                partialOutputText,
                compileChapterCheckbox->isChecked(),
                compileDocumentCheckbox->isChecked()
            );
            
            // Si l'option de compilation des chapitres est activée
            if (compileChapterCheckbox->isChecked()) {
                // Lancer la compilation des chapitres en parallèle
                latexAssembler->compileChapters(&model, chapterOutputText);
            }
            
            // Si l'option de compilation du document complet est activée
            if (compileDocumentCheckbox->isChecked()) {
                // Utiliser l'onglet "Document" pour la sortie
                outputTabWidget->setCurrentIndex(2);
                
                // Lancer la compilation du document complet
                latexAssembler->compileFullDocument(&model, documentOutputText);
            }
        }
    });

    // Ajouter les connexions pour les nouveaux signaux
    QObject::connect(latexAssembler, &LatexAssembler::fullDocumentCompilationStarted, [&]() {
        // On pourrait ajouter une indication visuelle si nécessaire
        documentOutputText->append("Compilation du document complet démarrée...\n");
    });

    QObject::connect(latexAssembler, &LatexAssembler::fullDocumentCompilationProgress, 
                   [documentOutputText](int current, int total) {
        documentOutputText->append(QString("Compilation %1/%2 en cours...\n").arg(current).arg(total));
    });

    QObject::connect(latexAssembler, &LatexAssembler::fullDocumentCompilationFinished, 
                   [documentOutputText, &lastPdfPath, openPdfButton](bool success, const QString& pdfPath) {
        if (success) {
            documentOutputText->append("\n=== Compilation du document complet terminée avec succès ===");
            documentOutputText->append("PDF créé : " + pdfPath);
            lastPdfPath = pdfPath;
            openPdfButton->setEnabled(true);
        } else {
            documentOutputText->append("\n=== Erreur lors de la compilation du document complet ===");
        }
    });

    QObject::connect(latexAssembler, &LatexAssembler::pdfAvailable, 
                    [&lastPdfPath, openPdfButton](const QString& pdfPath) {
        lastPdfPath = pdfPath;
        openPdfButton->setEnabled(true);
    });

    // Gérer les signaux de l'assembleur
    QObject::connect(latexAssembler, &LatexAssembler::compilationStarted, [&]() {
        compileButton->setEnabled(false);
        compileButton->setText("Compilation en cours...");
    });

    QObject::connect(latexAssembler, &LatexAssembler::compilationProgress, [&](int current, int total) {
        compileButton->setText(QString("Compilation en cours (%1/%2)...").arg(current).arg(total));
    });

    QObject::connect(latexAssembler, &LatexAssembler::compilationFinished, 
                    [compileButton, cancelButton, &lastPdfPath, openPdfButton](bool success, const QString& pdfPath) {
        // Toujours réactiver le bouton de compilation, qu'il y ait eu succès ou échec
        compileButton->setText("Compilation LuaLaTeX");
        compileButton->setEnabled(true);
        cancelButton->setEnabled(false); // Désactiver le bouton d'annulation
        
        if (success) {
            lastPdfPath = pdfPath;
            openPdfButton->setEnabled(true);
        }
    });

    QObject::connect(latexAssembler, &LatexAssembler::compilationError,
                    [compileButton](const QString& errorMessage) {
        // Réactiver le bouton en cas d'erreur
        compileButton->setText("Compilation LuaLaTeX");
        compileButton->setEnabled(true);
    });

    QObject::connect(latexAssembler, &LatexAssembler::chapterCompilationStarted, 
                   [chapterOutputText](const QString& chapterName) {
        // On pourrait ajouter une indication visuelle si nécessaire
    });

    QObject::connect(latexAssembler, &LatexAssembler::chapterCompilationFinished, 
                   [chapterOutputText](const QString& chapterName, bool success, const QString& pdfPath) {
        if (success) {
            chapterOutputText->append(QString("\nCompilation du chapitre %1 terminée avec succès !").arg(chapterName));
            chapterOutputText->append("PDF créé : " + pdfPath);
        } else {
            chapterOutputText->append(QString("\nErreur lors de la compilation du chapitre %1").arg(chapterName));
        }
    });

    QObject::connect(latexAssembler, &LatexAssembler::allChaptersCompiled, [chapterOutputText]() {
        chapterOutputText->append("\n=== Tous les chapitres ont été compilés ===");
    });

    QObject::connect(&app, &QApplication::aboutToQuit, [&]() {
        if (model.hasData()) {
            QJsonObject state = model.saveCheckState();
            lastFileHelper.saveCheckState(state);
        }
        
        // Sauvegarder également les options de compilation
        saveOptions();
    });
    
    window.setCentralWidget(centralWidget);
    window.show();
    return app.exec();
}