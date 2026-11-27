#include "Entrainement.h"

#include <pqxx/pqxx>
#include <utility>

#include "Db.h"
#include "Utilisateur.h"

using namespace std;

// --------------------------------------------------------------- Entrainement

Entrainement::Entrainement(int id, string dateHeure, string lieu, optional<int> equipeId, string description)
    : m_id(id),
      m_dateHeure(std::move(dateHeure)),
      m_lieu(std::move(lieu)),
      m_equipeId(equipeId),
      m_description(std::move(description))
{
}

// -------------------------------------------------------- EntrainementManager

namespace {

const char* SELECT_ENTRAINEMENT =
    "SELECT id, to_char(date_heure, 'YYYY-MM-DD HH24:MI') AS date_heure, lieu, equipe_id, description "
    "FROM entrainements ";

Entrainement depuisLigne(const pqxx::row& ligne)
{
    return Entrainement(ligne["id"].as<int>(),
                        ligne["date_heure"].as<string>(),
                        ligne["lieu"].as<string>(),
                        ligne["equipe_id"].as<optional<int>>(),
                        ligne["description"].as<string>());
}

void verifierChamps(const string& dateHeure, const string& lieu)
{
    if (dateHeure.empty() || lieu.empty()) {
        throw DonneeInvalide("La date et le lieu sont obligatoires.");
    }
}

vector<int> listeIds(pqxx::work& tx, const char* sql, int entrainementId)
{
    vector<int> ids;
    for (const auto& ligne : db::exec(tx, sql, entrainementId)) {
        ids.push_back(ligne[0].as<int>());
    }
    return ids;
}

} // namespace

EntrainementManager::EntrainementManager(string connexion) : Manager(std::move(connexion)) {}

Entrainement EntrainementManager::creer(const Utilisateur& auteur, const string& dateHeure, const string& lieu,
                                        optional<int> equipeId, const string& description)
{
    exigerAdmin(auteur, "creer un entrainement");
    verifierChamps(dateHeure, lieu);

    try {
        pqxx::connection conn(m_connexion);
        pqxx::work tx(conn);

        const int id = db::exec(tx,
            "INSERT INTO entrainements (date_heure, lieu, equipe_id, description) "
            "VALUES ($1::timestamp, $2, $3, $4) RETURNING id;",
            dateHeure, lieu, equipeId, description)[0][0].as<int>();

        if (equipeId) {
            db::exec(tx,
                "INSERT INTO entrainement_inscriptions (entrainement_id, utilisateur_id) "
                "SELECT $1, utilisateur_id FROM equipe_membres WHERE equipe_id = $2;",
                id, *equipeId);
        }

        Entrainement cree = depuisLigne(db::exec(tx, string(SELECT_ENTRAINEMENT) + "WHERE id = $1;", id)[0]);
        tx.commit();
        return cree;
    } catch (const pqxx::data_exception&) {
        throw DonneeInvalide("Date invalide. Format attendu : AAAA-MM-JJ HH:MM (ex : 2026-12-03 18:30).");
    } catch (const pqxx::foreign_key_violation&) {
        throw DonneeInvalide("Cette equipe n'existe pas.");
    }
}

void EntrainementManager::modifier(const Utilisateur& auteur, int entrainementId, const string& dateHeure,
                                   const string& lieu, optional<int> equipeId, const string& description)
{
    exigerAdmin(auteur, "modifier un entrainement");
    verifierChamps(dateHeure, lieu);

    try {
        pqxx::connection conn(m_connexion);
        pqxx::work tx(conn);
        const auto res = db::exec(tx,
            "UPDATE entrainements SET date_heure = $1::timestamp, lieu = $2, equipe_id = $3, description = $4 "
            "WHERE id = $5;",
            dateHeure, lieu, equipeId, description, entrainementId);
        if (res.affected_rows() == 0) {
            throw DonneeInvalide("Cet entrainement n'existe pas.");
        }
        tx.commit();
    } catch (const pqxx::data_exception&) {
        throw DonneeInvalide("Date invalide. Format attendu : AAAA-MM-JJ HH:MM (ex : 2026-12-03 18:30).");
    } catch (const pqxx::foreign_key_violation&) {
        throw DonneeInvalide("Cette equipe n'existe pas.");
    }
}

void EntrainementManager::supprimer(const Utilisateur& auteur, int entrainementId)
{
    exigerAdmin(auteur, "supprimer un entrainement");

    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);
    db::exec(tx, "DELETE FROM entrainements WHERE id = $1;", entrainementId);
    tx.commit();
}

vector<Entrainement> EntrainementManager::tous()
{
    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);

    vector<Entrainement> entrainements;
    for (const auto& ligne : tx.exec(string(SELECT_ENTRAINEMENT) + "ORDER BY date_heure;")) {
        entrainements.push_back(depuisLigne(ligne));
    }
    return entrainements;
}

optional<Entrainement> EntrainementManager::parId(int entrainementId)
{
    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);

    pqxx::result res = db::exec(tx, string(SELECT_ENTRAINEMENT) + "WHERE id = $1;", entrainementId);
    if (res.empty()) {
        return nullopt;
    }
    return depuisLigne(res[0]);
}

void EntrainementManager::inscrire(int entrainementId, int utilisateurId)
{
    try {
        pqxx::connection conn(m_connexion);
        pqxx::work tx(conn);
        db::exec(tx,
            "INSERT INTO entrainement_inscriptions (entrainement_id, utilisateur_id) VALUES ($1, $2) "
            "ON CONFLICT DO NOTHING;",
            entrainementId, utilisateurId);
        tx.commit();
    } catch (const pqxx::foreign_key_violation&) {
        throw DonneeInvalide("Cet entrainement n'existe pas.");
    }
}

void EntrainementManager::desinscrire(int entrainementId, int utilisateurId)
{
    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);
    db::exec(tx, "DELETE FROM entrainement_inscriptions WHERE entrainement_id = $1 AND utilisateur_id = $2;",
             entrainementId, utilisateurId);
    // Un joueur desinscrit n'est plus compte comme present.
    db::exec(tx, "DELETE FROM presences WHERE entrainement_id = $1 AND utilisateur_id = $2;",
             entrainementId, utilisateurId);
    tx.commit();
}

bool EntrainementManager::estInscrit(int entrainementId, int utilisateurId)
{
    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);
    return !db::exec(tx,
        "SELECT 1 FROM entrainement_inscriptions WHERE entrainement_id = $1 AND utilisateur_id = $2;",
        entrainementId, utilisateurId).empty();
}

vector<int> EntrainementManager::inscrits(int entrainementId)
{
    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);
    return listeIds(tx,
        "SELECT utilisateur_id FROM entrainement_inscriptions WHERE entrainement_id = $1 ORDER BY utilisateur_id;",
        entrainementId);
}

ResultatScan EntrainementManager::validerPresence(const Utilisateur& responsable, int entrainementId,
                                                  const string& contenuQr, optional<Utilisateur>& joueurScanne)
{
    exigerAdmin(responsable, "valider une presence");

    joueurScanne = UtilisateurManager(m_connexion).parContenuQrCode(contenuQr);
    if (!joueurScanne) {
        return ResultatScan::QrCodeInconnu;
    }
    const int joueurId = joueurScanne->getId();

    try {
        pqxx::connection conn(m_connexion);
        pqxx::work tx(conn);

        // Un joueur venu sans s'etre inscrit est inscrit automatiquement.
        const bool etaitInscrit = db::exec(tx,
            "INSERT INTO entrainement_inscriptions (entrainement_id, utilisateur_id) VALUES ($1, $2) "
            "ON CONFLICT DO NOTHING;",
            entrainementId, joueurId).affected_rows() == 0;

        const bool dejaPresent = db::exec(tx,
            "INSERT INTO presences (entrainement_id, utilisateur_id, scanne_par) VALUES ($1, $2, $3) "
            "ON CONFLICT DO NOTHING;",
            entrainementId, joueurId, responsable.getId()).affected_rows() == 0;

        tx.commit();

        if (dejaPresent) {
            return ResultatScan::DejaPresent;
        }
        return etaitInscrit ? ResultatScan::PresenceValidee : ResultatScan::AjouteEtValide;
    } catch (const pqxx::foreign_key_violation&) {
        throw DonneeInvalide("Cet entrainement n'existe pas.");
    }
}

vector<int> EntrainementManager::presents(int entrainementId)
{
    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);
    return listeIds(tx,
        "SELECT utilisateur_id FROM presences WHERE entrainement_id = $1 ORDER BY scanne_le;",
        entrainementId);
}

StatistiquesPresence EntrainementManager::statistiques(int entrainementId)
{
    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);

    const pqxx::row ligne = db::exec(tx,
        "SELECT (SELECT COUNT(*) FROM presences WHERE entrainement_id = $1), "
        "       (SELECT COUNT(*) FROM entrainement_inscriptions WHERE entrainement_id = $1);",
        entrainementId)[0];

    StatistiquesPresence stats;
    stats.presents = ligne[0].as<int>();
    stats.inscrits = ligne[1].as<int>();
    return stats;
}

vector<LignePresence> EntrainementManager::feuilleDePresence(int entrainementId)
{
    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);

    vector<LignePresence> feuille;
    for (const auto& ligne : db::exec(tx,
             "SELECT u.id, "
             "       CASE WHEN u.prenom = '' AND u.nom = '' THEN 'licence ' || u.numero_licence "
             "            ELSE u.prenom || ' ' || u.nom END AS nom_complet, "
             "       COALESCE(to_char(p.scanne_le, 'HH24:MI'), '') AS heure_scan "
             "FROM entrainement_inscriptions i "
             "JOIN utilisateurs u ON u.id = i.utilisateur_id "
             "LEFT JOIN presences p ON p.entrainement_id = i.entrainement_id AND p.utilisateur_id = i.utilisateur_id "
             "WHERE i.entrainement_id = $1 "
             "ORDER BY p.scanne_le IS NULL, p.scanne_le, u.nom, u.prenom;",
             entrainementId)) {
        LignePresence presence;
        presence.utilisateurId = ligne["id"].as<int>();
        presence.nomComplet = ligne["nom_complet"].as<string>();
        presence.heureScan = ligne["heure_scan"].as<string>();
        feuille.push_back(presence);
    }
    return feuille;
}
