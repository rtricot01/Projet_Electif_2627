#ifndef MATCH_H_INCLUDED
#define MATCH_H_INCLUDED

#include <optional>
#include <string>
#include <vector>

#include "Manager.h"

class Utilisateur;

// Type de competition d'un match.
enum class TypeCompetition { CFE, ACAD };

std::string competitionVersTexte(TypeCompetition type);
TypeCompetition competitionDepuisTexte(const std::string& texte);

// Score d'un set : points de notre equipe et points de l'adversaire.
struct ScoreSet {
    int pointsEquipe = 0;
    int pointsAdversaire = 0;
};

// Un match d'une equipe de l'AS Volley contre un adversaire.
class Match {
private:
    int m_id = 0;
    std::string m_dateHeure;          // format "AAAA-MM-JJ HH:MM"
    std::string m_lieu;
    int m_equipeId = 0;
    std::string m_adversaire;
    TypeCompetition m_competition = TypeCompetition::CFE;
    std::string m_description;
    std::vector<ScoreSet> m_sets;     // detail du score, set par set (vide si pas encore joue)

public:
    Match() = default;
    Match(int id, std::string dateHeure, std::string lieu, int equipeId, std::string adversaire,
          TypeCompetition competition, std::string description, std::vector<ScoreSet> sets);

    int getId() const { return m_id; }
    const std::string& getDateHeure() const { return m_dateHeure; }
    const std::string& getLieu() const { return m_lieu; }
    int getEquipeId() const { return m_equipeId; }
    const std::string& getAdversaire() const { return m_adversaire; }
    TypeCompetition getCompetition() const { return m_competition; }
    const std::string& getDescription() const { return m_description; }
    const std::vector<ScoreSet>& getSets() const { return m_sets; }

    bool estJoue() const { return !m_sets.empty(); }
    int setsGagnes() const;
    int setsPerdus() const;
    // Ex : "3-1 (25-20, 22-25, 25-18, 25-23)" ou "a jouer".
    std::string scoreTexte() const;
};

// Acces aux tables "matchs" et "match_sets". Reserve aux admins pour les modifications.
class MatchManager : public Manager {
public:
    explicit MatchManager(std::string connexion);

    Match creer(const Utilisateur& auteur, const std::string& dateHeure, const std::string& lieu, int equipeId,
                const std::string& adversaire, TypeCompetition competition, const std::string& description);
    void modifier(const Utilisateur& auteur, int matchId, const std::string& dateHeure, const std::string& lieu,
                  int equipeId, const std::string& adversaire, TypeCompetition competition,
                  const std::string& description);
    // Remplace le detail du score (liste vide = score efface).
    void enregistrerScore(const Utilisateur& auteur, int matchId, const std::vector<ScoreSet>& sets);
    void supprimer(const Utilisateur& auteur, int matchId);

    std::vector<Match> tous();
    std::optional<Match> parId(int matchId);
};

#endif // MATCH_H_INCLUDED
