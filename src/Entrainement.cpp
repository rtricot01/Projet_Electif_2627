#include "Entrainement.h"

#include <pqxx/pqxx>
#include <utility>

#include "Db.h"
#include "Utilisateur.h"

using namespace std;

string typeSeanceVersTexte(TypeSeance type)
{
    switch (type) {
    case TypeSeance::A: return "A";
    case TypeSeance::B: return "B";
    case TypeSeance::Ouverte: break;
    }
    return "ouverte";
}

TypeSeance typeSeanceDepuisTexte(const string& texte)
{
    if (texte == "A") {
        return TypeSeance::A;
    }
    if (texte == "B") {
        return TypeSeance::B;
    }
    return TypeSeance::Ouverte;
}

// --------------------------------------------------------------- Entrainement

Entrainement::Entrainement(int id, ParametresSeance parametres, string ouvertureCompetition,
                           bool competitionOuverte, bool commencee)
    : m_id(id),
      m_parametres(std::move(parametres)),
      m_ouvertureCompetition(std::move(ouvertureCompetition)),
      m_competitionOuverte(competitionOuverte),
      m_commencee(commencee)
{
}

optional<string> Entrainement::raisonRefusInscription(const Utilisateur& joueur, int nbInscrits) const
{
    const bool competition = joueur.typeJoueur() == TypeJoueur::Competition;

    if (m_commencee) {
        return string("La seance a deja commence.");
    }
    if (m_parametres.type == TypeSeance::B && !competition) {
        return string("Semaine B : seance reservee aux joueurs competition.");
    }
    if (m_parametres.type == TypeSeance::A && competition && !m_competitionOuverte) {
        return "Semaine A : les joueurs loisirs sont prioritaires. Inscriptions ouvertes pour vous a partir du "
               + m_ouvertureCompetition + ".";
    }
    if (nbInscrits >= m_parametres.places) {
        return "Seance complete (" + to_string(m_parametres.places) + " places).";
    }
    return nullopt;
}

// -------------------------------------------------------- EntrainementManager

namespace {

// La base calcule aussi l'ouverture des inscriptions competition (semaine A)
// et si la seance a commence, a partir de l'heure actuelle.
const string SELECT_ENTRAINEMENT =
    "SELECT id, to_char(date_heure, 'YYYY-MM-DD HH24:MI') AS date_heure, lieu, equipe_id, description, "
    "  type_seance, places, "
    "  to_char(date_heure - make_interval(hours => " + to_string(Entrainement::HEURES_PRIORITE_LOISIRS) + "), "
    "          'DD/MM/YYYY a HH24:MI') AS ouverture_competition, "
    "  now() >= date_heure - make_interval(hours => " + to_string(Entrainement::HEURES_PRIORITE_LOISIRS) + ") "
    "      AS competition_ouverte, "
    "  now() >= date_heure AS commencee "
    "FROM entrainements ";

Entrainement depuisLigne(const pqxx::row& ligne)
{
    ParametresSeance parametres;
    parametres.dateHeure = ligne["date_heure"].as<string>();
    parametres.lieu = ligne["lieu"].as<string>();
    parametres.equipeId = ligne["equipe_id"].as<optional<int>>();
    parametres.description = ligne["description"].as<string>();
    parametres.type = typeSeanceDepuisTexte(ligne["type_seance"].as<string>());
    parametres.places = ligne["places"].as<int>();
    return Entrainement(ligne["id"].as<int>(), parametres,
                        ligne["ouverture_competition"].as<string>(),
                        ligne["competition_ouverte"].as<bool>(),
                        ligne["commencee"].as<bool>());
}

void verifierChamps(const ParametresSeance& parametres)
{
    if (parametres.dateHeure.empty() || parametres.lieu.empty()) {
        throw DonneeInvalide("La date et le lieu sont obligatoires.");
    }
    if (parametres.places < 1) {
        throw DonneeInvalide("Il faut au moins une place.");
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

Entrainement EntrainementManager::creer(const Utilisateur& auteur, const ParametresSeance& parametres)
{
    exigerAdmin(auteur, "creer un entrainement");
    verifierChamps(parametres);

    try {
        pqxx::connection conn(m_connexion);
        pqxx::work tx(conn);

        const int id = db::exec(tx,
            "INSERT INTO entrainements (date_heure, lieu, equipe_id, description, type_seance, places) "
            "VALUES ($1::timestamp, $2, $3, $4, $5, $6) RETURNING id;",
            parametres.dateHeure, parametres.lieu, parametres.equipeId, parametres.description,
            typeSeanceVersTexte(parametres.type), parametres.places)[0][0].as<int>();

        if (parametres.equipeId) {
            db::exec(tx,
                "INSERT INTO entrainement_inscriptions (entrainement_id, utilisateur_id) "
                "SELECT $1, utilisateur_id FROM equipe_membres WHERE equipe_id = $2;",
                id, *parametres.equipeId);
        }

        Entrainement cree = depuisLigne(db::exec(tx, SELECT_ENTRAINEMENT + "WHERE id = $1;", id)[0]);
        tx.commit();
        return cree;
    } catch (const pqxx::data_exception&) {
        throw DonneeInvalide("Date invalide. Format attendu : AAAA-MM-JJ HH:MM (ex : 2026-12-03 18:30).");
    } catch (const pqxx::foreign_key_violation&) {
        throw DonneeInvalide("Cette equipe n'existe pas.");
    }
}

void EntrainementManager::modifier(const Utilisateur& auteur, int entrainementId, const ParametresSeance& parametres)
{
    exigerAdmin(auteur, "modifier un entrainement");
    verifierChamps(parametres);

    try {
        pqxx::connection conn(m_connexion);
        pqxx::work tx(conn);
        const auto res = db::exec(tx,
            "UPDATE entrainements SET date_heure = $1::timestamp, lieu = $2, equipe_id = $3, description = $4, "
            "  type_seance = $5, places = $6 WHERE id = $7;",
            parametres.dateHeure, parametres.lieu, parametres.equipeId, parametres.description,
            typeSeanceVersTexte(parametres.type), parametres.places, entrainementId);
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
    for (const auto& ligne : tx.exec(SELECT_ENTRAINEMENT + "ORDER BY date_heure;")) {
        entrainements.push_back(depuisLigne(ligne));
    }
    return entrainements;
}

optional<Entrainement> EntrainementManager::parId(int entrainementId)
{
    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);

    pqxx::result res = db::exec(tx, SELECT_ENTRAINEMENT + "WHERE id = $1;", entrainementId);
    if (res.empty()) {
        return nullopt;
    }
    return depuisLigne(res[0]);
}

void EntrainementManager::inscrire(int entrainementId, const Utilisateur& joueur)
{
    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);

    // Verrou sur la seance : deux joueurs ne peuvent pas prendre la derniere place en meme temps.
    pqxx::result seance = db::exec(tx, SELECT_ENTRAINEMENT + "WHERE id = $1 FOR UPDATE;", entrainementId);
    if (seance.empty()) {
        throw DonneeInvalide("Cet entrainement n'existe pas.");
    }
    const Entrainement entrainement = depuisLigne(seance[0]);

    const bool dejaInscrit = !db::exec(tx,
        "SELECT 1 FROM entrainement_inscriptions WHERE entrainement_id = $1 AND utilisateur_id = $2;",
        entrainementId, joueur.getId()).empty();
    if (dejaInscrit) {
        return;
    }

    const int nbInscrits = db::exec(tx,
        "SELECT COUNT(*) FROM entrainement_inscriptions WHERE entrainement_id = $1;", entrainementId)[0][0].as<int>();
    if (const optional<string> raison = entrainement.raisonRefusInscription(joueur, nbInscrits)) {
        throw DonneeInvalide(*raison);
    }

    db::exec(tx, "INSERT INTO entrainement_inscriptions (entrainement_id, utilisateur_id) VALUES ($1, $2);",
             entrainementId, joueur.getId());
    tx.commit();
}

void EntrainementManager::ajouterInscrit(const Utilisateur& auteur, int entrainementId, int utilisateurId)
{
    exigerAdmin(auteur, "ajouter un joueur a une seance");
    try {
        pqxx::connection conn(m_connexion);
        pqxx::work tx(conn);
        db::exec(tx,
            "INSERT INTO entrainement_inscriptions (entrainement_id, utilisateur_id) VALUES ($1, $2) "
            "ON CONFLICT DO NOTHING;",
            entrainementId, utilisateurId);
        tx.commit();
    } catch (const pqxx::foreign_key_violation&) {
        throw DonneeInvalide("Seance ou joueur introuvable.");
    }
}

void EntrainementManager::retirerInscrit(const Utilisateur& auteur, int entrainementId, int utilisateurId)
{
    exigerAdmin(auteur, "retirer un joueur d'une seance");
    desinscrire(entrainementId, utilisateurId);
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
             "       COALESCE(to_char(p.scanne_le, 'HH24:MI'), '') AS heure_scan, "
             "       (u.type_joueur = 'competition' "
             "        OR EXISTS (SELECT 1 FROM equipe_membres m WHERE m.utilisateur_id = u.id)) AS competition "
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
        presence.competition = ligne["competition"].as<bool>();
        feuille.push_back(presence);
    }
    return feuille;
}

vector<int> EntrainementManager::inscriptionsDe(int utilisateurId)
{
    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);
    return listeIds(tx,
        "SELECT entrainement_id FROM entrainement_inscriptions WHERE utilisateur_id = $1;", utilisateurId);
}

map<int, StatistiquesPresence> EntrainementManager::statistiquesParEntrainement()
{
    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);

    map<int, StatistiquesPresence> resultat;
    for (const auto& ligne : tx.exec(
             "SELECT e.id, "
             "  (SELECT COUNT(*) FROM presences p WHERE p.entrainement_id = e.id), "
             "  (SELECT COUNT(*) FROM entrainement_inscriptions i WHERE i.entrainement_id = e.id) "
             "FROM entrainements e;")) {
        StatistiquesPresence stats;
        stats.presents = ligne[1].as<int>();
        stats.inscrits = ligne[2].as<int>();
        resultat[ligne[0].as<int>()] = stats;
    }
    return resultat;
}
