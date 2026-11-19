#ifndef ENTRAINEMENT_H_INCLUDED
#define ENTRAINEMENT_H_INCLUDED

#include <optional>
#include <string>
#include <vector>

#include "Manager.h"

class Utilisateur;

// Une seance d'entrainement.
class Entrainement {
private:
    int m_id = 0;
    std::string m_dateHeure;          // format "AAAA-MM-JJ HH:MM"
    std::string m_lieu;
    std::optional<int> m_equipeId;    // equipe concernee (facultatif)
    std::string m_description;

public:
    Entrainement() = default;
    Entrainement(int id, std::string dateHeure, std::string lieu,
                 std::optional<int> equipeId, std::string description);

    int getId() const { return m_id; }
    const std::string& getDateHeure() const { return m_dateHeure; }
    const std::string& getLieu() const { return m_lieu; }
    const std::optional<int>& getEquipeId() const { return m_equipeId; }
    const std::string& getDescription() const { return m_description; }
};

// Nombre de presents sur nombre d'inscrits pour une seance.
struct StatistiquesPresence {
    int presents = 0;
    int inscrits = 0;

    // Pourcentage de presents (0 si personne n'est inscrit).
    double taux() const { return inscrits == 0 ? 0.0 : 100.0 * presents / inscrits; }
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
    Entrainement creer(const Utilisateur& auteur, const std::string& dateHeure, const std::string& lieu,
                       std::optional<int> equipeId, const std::string& description);
    void modifier(const Utilisateur& auteur, int entrainementId, const std::string& dateHeure,
                  const std::string& lieu, std::optional<int> equipeId, const std::string& description);
    void supprimer(const Utilisateur& auteur, int entrainementId);

    std::vector<Entrainement> tous();
    std::optional<Entrainement> parId(int entrainementId);

    // --- Inscriptions (chaque membre peut s'inscrire lui-meme) ---
    void inscrire(int entrainementId, int utilisateurId);
    void desinscrire(int entrainementId, int utilisateurId);
    bool estInscrit(int entrainementId, int utilisateurId);
    std::vector<int> inscrits(int entrainementId);

    // --- Presences (admin uniquement) ---
    // Le responsable scanne le QR code d'un joueur au debut de la seance.
    // contenuQr est le texte lu dans le QR code. joueurScanne recoit le joueur reconnu.
    ResultatScan validerPresence(const Utilisateur& responsable, int entrainementId,
                                 const std::string& contenuQr, std::optional<Utilisateur>& joueurScanne);
    std::vector<int> presents(int entrainementId);
    StatistiquesPresence statistiques(int entrainementId);
};

#endif // ENTRAINEMENT_H_INCLUDED
