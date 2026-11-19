#include "Match.h"

#include <pqxx/pqxx>
#include <utility>

#include "Db.h"
#include "Utilisateur.h"

using namespace std;

string competitionVersTexte(TypeCompetition type)
{
    return type == TypeCompetition::ACAD ? "ACAD" : "CFE";
}

TypeCompetition competitionDepuisTexte(const string& texte)
{
    return texte == "ACAD" ? TypeCompetition::ACAD : TypeCompetition::CFE;
}

// ---------------------------------------------------------------------- Match

Match::Match(int id, string dateHeure, string lieu, int equipeId, string adversaire,
             TypeCompetition competition, string description, vector<ScoreSet> sets)
    : m_id(id),
      m_dateHeure(std::move(dateHeure)),
      m_lieu(std::move(lieu)),
      m_equipeId(equipeId),
      m_adversaire(std::move(adversaire)),
      m_competition(competition),
      m_description(std::move(description)),
      m_sets(std::move(sets))
{
}

int Match::setsGagnes() const
{
    int total = 0;
    for (const ScoreSet& set : m_sets) {
        if (set.pointsEquipe > set.pointsAdversaire) {
            ++total;
        }
    }
    return total;
}

int Match::setsPerdus() const
{
    int total = 0;
    for (const ScoreSet& set : m_sets) {
        if (set.pointsAdversaire > set.pointsEquipe) {
            ++total;
        }
    }
    return total;
}

string Match::scoreTexte() const
{
    if (!estJoue()) {
        return "a jouer";
    }
    string texte = to_string(setsGagnes()) + "-" + to_string(setsPerdus()) + " (";
    for (size_t i = 0; i < m_sets.size(); ++i) {
        if (i > 0) {
            texte += ", ";
        }
        texte += to_string(m_sets[i].pointsEquipe) + "-" + to_string(m_sets[i].pointsAdversaire);
    }
    return texte + ")";
}

// --------------------------------------------------------------- MatchManager

namespace {

const char* SELECT_MATCH =
    "SELECT id, to_char(date_heure, 'YYYY-MM-DD HH24:MI') AS date_heure, lieu, equipe_id, adversaire, "
    "type_competition, description FROM matchs ";

vector<ScoreSet> lireSets(pqxx::work& tx, int matchId)
{
    vector<ScoreSet> sets;
    for (const auto& ligne : db::exec(tx,
             "SELECT points_equipe, points_adversaire FROM match_sets WHERE match_id = $1 ORDER BY numero;",
             matchId)) {
        sets.push_back({ligne[0].as<int>(), ligne[1].as<int>()});
    }
    return sets;
}

Match depuisLigne(pqxx::work& tx, const pqxx::row& ligne)
{
    const int id = ligne["id"].as<int>();
    return Match(id,
                 ligne["date_heure"].as<string>(),
                 ligne["lieu"].as<string>(),
                 ligne["equipe_id"].as<int>(),
                 ligne["adversaire"].as<string>(),
                 competitionDepuisTexte(ligne["type_competition"].as<string>()),
                 ligne["description"].as<string>(),
                 lireSets(tx, id));
}

void verifierChamps(const string& dateHeure, const string& lieu, const string& adversaire)
{
    if (dateHeure.empty() || lieu.empty() || adversaire.empty()) {
        throw DonneeInvalide("La date, le lieu et l'adversaire sont obligatoires.");
    }
}

void verifierSets(const vector<ScoreSet>& sets)
{
    if (sets.size() > 5) {
        throw DonneeInvalide("Un match de volley se joue en 5 sets maximum.");
    }
    for (const ScoreSet& set : sets) {
        if (set.pointsEquipe < 0 || set.pointsAdversaire < 0) {
            throw DonneeInvalide("Un score ne peut pas etre negatif.");
        }
        if (set.pointsEquipe == set.pointsAdversaire) {
            throw DonneeInvalide("Un set ne peut pas finir sur une egalite.");
        }
    }
}

const string MESSAGE_DATE = "Date invalide. Format attendu : AAAA-MM-JJ HH:MM (ex : 2026-12-03 18:30).";

} // namespace

MatchManager::MatchManager(string connexion) : Manager(std::move(connexion)) {}

Match MatchManager::creer(const Utilisateur& auteur, const string& dateHeure, const string& lieu, int equipeId,
                          const string& adversaire, TypeCompetition competition, const string& description)
{
    exigerAdmin(auteur, "creer un match");
    verifierChamps(dateHeure, lieu, adversaire);

    try {
        pqxx::connection conn(m_connexion);
        pqxx::work tx(conn);
        const int id = db::exec(tx,
            "INSERT INTO matchs (date_heure, lieu, equipe_id, adversaire, type_competition, description) "
            "VALUES ($1::timestamp, $2, $3, $4, $5, $6) RETURNING id;",
            dateHeure, lieu, equipeId, adversaire, competitionVersTexte(competition), description)[0][0].as<int>();
        Match cree = depuisLigne(tx, db::exec(tx, string(SELECT_MATCH) + "WHERE id = $1;", id)[0]);
        tx.commit();
        return cree;
    } catch (const pqxx::data_exception&) {
        throw DonneeInvalide(MESSAGE_DATE);
    } catch (const pqxx::foreign_key_violation&) {
        throw DonneeInvalide("Cette equipe n'existe pas.");
    }
}

void MatchManager::modifier(const Utilisateur& auteur, int matchId, const string& dateHeure, const string& lieu,
                            int equipeId, const string& adversaire, TypeCompetition competition,
                            const string& description)
{
    exigerAdmin(auteur, "modifier un match");
    verifierChamps(dateHeure, lieu, adversaire);

    try {
        pqxx::connection conn(m_connexion);
        pqxx::work tx(conn);
        const auto res = db::exec(tx,
            "UPDATE matchs SET date_heure = $1::timestamp, lieu = $2, equipe_id = $3, adversaire = $4, "
            "type_competition = $5, description = $6 WHERE id = $7;",
            dateHeure, lieu, equipeId, adversaire, competitionVersTexte(competition), description, matchId);
        if (res.affected_rows() == 0) {
            throw DonneeInvalide("Ce match n'existe pas.");
        }
        tx.commit();
    } catch (const pqxx::data_exception&) {
        throw DonneeInvalide(MESSAGE_DATE);
    } catch (const pqxx::foreign_key_violation&) {
        throw DonneeInvalide("Cette equipe n'existe pas.");
    }
}

void MatchManager::enregistrerScore(const Utilisateur& auteur, int matchId, const vector<ScoreSet>& sets)
{
    exigerAdmin(auteur, "saisir un score");
    verifierSets(sets);

    try {
        pqxx::connection conn(m_connexion);
        pqxx::work tx(conn);
        db::exec(tx, "DELETE FROM match_sets WHERE match_id = $1;", matchId);
        for (size_t i = 0; i < sets.size(); ++i) {
            db::exec(tx,
                "INSERT INTO match_sets (match_id, numero, points_equipe, points_adversaire) VALUES ($1, $2, $3, $4);",
                matchId, static_cast<int>(i + 1), sets[i].pointsEquipe, sets[i].pointsAdversaire);
        }
        tx.commit();
    } catch (const pqxx::foreign_key_violation&) {
        throw DonneeInvalide("Ce match n'existe pas.");
    }
}

void MatchManager::supprimer(const Utilisateur& auteur, int matchId)
{
    exigerAdmin(auteur, "supprimer un match");

    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);
    db::exec(tx, "DELETE FROM matchs WHERE id = $1;", matchId);
    tx.commit();
}

vector<Match> MatchManager::tous()
{
    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);

    vector<Match> matchs;
    for (const auto& ligne : tx.exec(string(SELECT_MATCH) + "ORDER BY date_heure;")) {
        matchs.push_back(depuisLigne(tx, ligne));
    }
    return matchs;
}

optional<Match> MatchManager::parId(int matchId)
{
    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);

    pqxx::result res = db::exec(tx, string(SELECT_MATCH) + "WHERE id = $1;", matchId);
    if (res.empty()) {
        return nullopt;
    }
    return depuisLigne(tx, res[0]);
}
