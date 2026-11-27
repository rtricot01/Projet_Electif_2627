#ifndef APPLICATION_H_INCLUDED
#define APPLICATION_H_INCLUDED

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "Entrainement.h"
#include "Equipe.h"
#include "Match.h"
#include "Utilisateur.h"

// Interface console du programme : menus, saisies et affichages.
// Toute la logique (droits admin, base de donnees) est dans les classes *Manager,
// pour pouvoir la reutiliser plus tard dans le site web.
class Application {
private:
    UtilisateurManager m_utilisateurs;
    EquipeManager m_equipes;
    EntrainementManager m_entrainements;
    MatchManager m_matchs;
    std::optional<Utilisateur> m_connecte; // membre actuellement connecte

    // Une ligne d'un menu.
    struct Option {
        std::string libelle;
        std::function<void()> action;
        bool adminSeulement = false;
        std::function<bool()> condition = nullptr; // option affichee seulement si condition() est vraie
    };

    void afficherMenu(const std::string& titre, const std::vector<Option>& options);
    bool estAdmin() const { return m_connecte && m_connecte->estAdmin(); }
    void rechargerUtilisateurConnecte();

    // Accueil
    void connexion();
    void premiereConnexion(const std::string& licence);
    void creerCompteAdmin();
    std::string choisirMotDePasse();
    void menuPrincipal();

    // Profil
    void monProfil();
    void monQrCode();

    // Membres (admin)
    void afficherMembres();
    void importerJoueurs();
    void reinitialiserMotDePasse();
    void changerStatutMembre();
    void supprimerMembre();

    // Equipes
    void afficherEquipes();
    void creerEquipe();
    void modifierEquipe();
    void supprimerEquipe();
    void changerPhotoEquipe();
    void exporterPhotoEquipe();

    // Entrainements
    void afficherEntrainements();
    void creerEntrainement();
    void modifierEntrainement();
    void supprimerEntrainement();
    void sInscrireEntrainement();
    void seDesinscrireEntrainement();
    void scannerPresences();
    void voirPresences();
    void afficherFeuilleDePresence(int entrainementId);

    // Matchs
    void afficherMatchs();
    void creerMatch();
    void modifierMatch();
    void saisirScore();
    void supprimerMatch();

    // Aides a la saisie
    static std::string lireTexte(const std::string& question);
    static std::string lireTexte(const std::string& question, const std::string& valeurActuelle);
    static std::optional<int> lireEntier(const std::string& question);
    static bool confirmer(const std::string& question);
    std::vector<int> choisirJoueurs(const std::vector<int>& actuels);
    std::optional<int> choisirEquipe(bool facultatif, std::optional<int> actuelle = std::nullopt);
    static TypeCompetition choisirCompetition(std::optional<TypeCompetition> actuelle = std::nullopt);
    std::string nomJoueur(int utilisateurId);
    std::string nomEquipe(std::optional<int> equipeId);

public:
    explicit Application(const std::string& connexion);

    // Boucle principale du programme.
    void lancer();
};

#endif // APPLICATION_H_INCLUDED
