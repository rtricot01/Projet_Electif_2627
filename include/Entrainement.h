#ifndef ENTRAINEMENT_H_INCLUDED
#define ENTRAINEMENT_H_INCLUDED

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "Manager.h"

class Utilisateur;

// Qui peut s'inscrire a une seance.
enum class TypeSeance {
    Ouverte,   // tout le monde, sans priorite
    A,         // semaine A : priorite aux loisirs, les competiteurs completent 48 h avant
    B          // semaine B : uniquement les joueurs competition
};

std::string typeSeanceVersTexte(TypeSeance type);
TypeSeance typeSeanceDepuisTexte(const std::string& texte);

// Ce que l'admin saisit pour creer ou modifier une seance.
struct ParametresSeance {
    std::string dateHeure;            // "AAAA-MM-JJ HH:MM"
    std::string lieu;
    std::optional<int> equipeId;      // equipe concernee (facultatif)
    std::string description;
    TypeSeance type = TypeSeance::Ouverte;
    int places = 50;                  // nombre maximum d'inscrits
};

// Une seance d'entrainement.
class Entrainement {
private:
    int m_id = 0;
    ParametresSeance m_parametres;
    // Calcule par la base au moment de la lecture :
    std::string m_ouvertureCompetition; // semaine A : date a partir de laquelle les competiteurs s'inscrivent
    bool m_competitionOuverte = true;   // semaine A : cette date est-elle passee ?
    bool m_commencee = false;           // la seance a-t-elle deja commence ?

public:
    // Semaine A : les joueurs competition peuvent s'inscrire ce nombre d'heures avant la seance.
    static constexpr int HEURES_PRIORITE_LOISIRS = 48;

    Entrainement() = default;
    Entrainement(int id, ParametresSeance parametres, std::string ouvertureCompetition,
                 bool competitionOuverte, bool commencee);

    int getId() const { return m_id; }
    const ParametresSeance& getParametres() const { return m_parametres; }
    const std::string& getDateHeure() const { return m_parametres.dateHeure; }
    const std::string& getLieu() const { return m_parametres.lieu; }
    const std::optional<int>& getEquipeId() const { return m_parametres.equipeId; }
    const std::string& getDescription() const { return m_parametres.description; }
    TypeSeance getType() const { return m_parametres.type; }
    int getPlaces() const { return m_parametres.places; }
    const std::string& getOuvertureCompetition() const { return m_ouvertureCompetition; }
    bool estCommencee() const { return m_commencee; }

    // Pourquoi ce joueur ne peut pas s'inscrire lui-meme (rien = il peut).
    // nbInscrits : nombre d'inscrits actuel de la seance.
    std::optional<std::string> raisonRefusInscription(const Utilisateur& joueur, int nbInscrits) const;
};

// Nombre de presents sur nombre d'inscrits pour une seance.
struct StatistiquesPresence {
    int presents = 0;
    int inscrits = 0;

    // Pourcentage de presents (0 si personne n'est inscrit).
    double taux() const { return inscrits == 0 ? 0.0 : 100.0 * presents / inscrits; }
};

// Une ligne de la feuille de presence d'une seance.
struct LignePresence {
    int utilisateurId = 0;
    std::string nomComplet;
    bool competition = false;  // joueur competition (sinon loisir)
    std::string heureScan;   // "HH:MM" ; vide si le joueur n'a pas ete scanne (absent)

    bool estPresent() const { return !heureScan.empty(); }
};

// Resultat du scan d'un QR code par un responsable.
enum class ResultatScan {
    PresenceValidee,   // joueur inscrit, presence enregistree
    AjouteEtValide,    // joueur pas inscrit : il est inscrit puis marque present
    DejaPresent,       // le QR code a deja ete scanne pour cette seance
    QrCodeInconnu      // ce QR code ne correspond a aucun membre
};

// Acces aux tables "entrainements", "entrainement_inscriptions" et "presences".
class EntrainementManager : public Manager {
public:
    explicit EntrainementManager(std::string connexion);

    // --- Gestion des seances (admin uniquement) ---
    // Si une equipe est donnee, tous ses joueurs sont inscrits automatiquement.
    Entrainement creer(const Utilisateur& auteur, const ParametresSeance& parametres);
    void modifier(const Utilisateur& auteur, int entrainementId, const ParametresSeance& parametres);
    void supprimer(const Utilisateur& auteur, int entrainementId);

    std::vector<Entrainement> tous();
    std::optional<Entrainement> parId(int entrainementId);

    // --- Inscriptions par le joueur lui-meme ---
    // Respecte les places et les regles des semaines A / B (leve DonneeInvalide sinon).
    void inscrire(int entrainementId, const Utilisateur& joueur);
    void desinscrire(int entrainementId, int utilisateurId);

    // --- Gestion de la liste des inscrits (admin uniquement) ---
    // L'admin peut ajouter un joueur meme si la seance est pleine ou reservee.
    void ajouterInscrit(const Utilisateur& auteur, int entrainementId, int utilisateurId);
    void retirerInscrit(const Utilisateur& auteur, int entrainementId, int utilisateurId);
    bool estInscrit(int entrainementId, int utilisateurId);
    std::vector<int> inscrits(int entrainementId);
    // Id des entrainements auxquels ce membre est inscrit.
    std::vector<int> inscriptionsDe(int utilisateurId);

    // --- Presences (admin uniquement) ---
    // Le responsable scanne le QR code d'un joueur au debut de la seance.
    // contenuQr est le texte lu dans le QR code. joueurScanne recoit le joueur reconnu.
    ResultatScan validerPresence(const Utilisateur& responsable, int entrainementId,
                                 const std::string& contenuQr, std::optional<Utilisateur>& joueurScanne);
    std::vector<int> presents(int entrainementId);
    StatistiquesPresence statistiques(int entrainementId);
    // Statistiques de toutes les seances en une seule requete (cle = id de l'entrainement).
    std::map<int, StatistiquesPresence> statistiquesParEntrainement();

    // Liste des inscrits avec leur nom : d'abord les presents (dans l'ordre des scans),
    // puis les absents (par ordre alphabetique). Une seule requete, meme avec 50 joueurs.
    std::vector<LignePresence> feuilleDePresence(int entrainementId);
};

#endif // ENTRAINEMENT_H_INCLUDED
