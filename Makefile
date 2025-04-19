.PHONY: all clean rebuild run

# Variables
BUILD_DIR = build
CMAKE_OPTIONS = ..
MAKE_OPTIONS = -j8
EXEC = $(BUILD_DIR)/LatexTreeViewer

# Objectif par défaut
all: $(EXEC)

# Création et compilation dans le répertoire build
$(EXEC):
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake $(CMAKE_OPTIONS)
	@cd $(BUILD_DIR) && make $(MAKE_OPTIONS)
	@echo "Build terminé avec succès !"

# Nettoyage complet
clean:
	@echo "Nettoyage du répertoire de build..."
	@rm -rf $(BUILD_DIR)
	@echo "Nettoyage terminé."

# Reconstruction complète
rebuild: clean all

# Exécution du programme
run: all
	@echo "Lancement de l'application..."
	@$(EXEC)